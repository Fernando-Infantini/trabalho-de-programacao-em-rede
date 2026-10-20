#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#include "parser.h"


#define MYADDR "0.0.0.0"
#define MYPORT 9998
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 50
#define BANDA_PADRAO 1000
#define ARQUIVO_QOS_PADRAO "qos_config.txt"

// Guarda os dados de cada conexao que esta sendo atendida.
struct cliente_ativo {
    int ativo; // Indica se esta posicao da lista esta sendo usada.
    int socket_fd; // Socket usado para conversar com o cliente.
    char ip[INET_ADDRSTRLEN]; // IP de origem da conexao.
    double rtt_ms; // Ultimo RTT estimado para esse cliente.
    double banda_estimada_kbps; // Banda observada durante o envio.
    int taxa_base_kbps; // Limite total configurado para este IP.
    int taxa_kbps; // Parte da taxa que ficou para esta conexao.
};

// Guarda o estado usado para medir o intervalo entre o HTML e o primeiro objeto.
struct estado_ip {
    int usado; // Indica se esta posicao ja pertence a algum IP.
    char ip[INET_ADDRSTRLEN]; // IP relacionado a esta medicao.
    int aguardando_primeiro_objeto; // Fica 1 depois que o HTML e solicitado.
    struct timeval instante_html; // Momento em que o pedido do HTML chegou.
    double rtt_ms; // Ultimo RTT calculado.
    double banda_estimada_kbps; // Ultima banda estimada.
};

// As threads compartilham essas listas, por isso o acesso e protegido com mutex.
struct cliente_ativo lista_clientes[MAX_CLIENTS];
struct estado_ip lista_estados_ip[MAX_CLIENTS];

pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

int capacidade_maxima = 10000;
const char *arquivo_qos = ARQUIVO_QOS_PADRAO;


// Prototipos das funcoes usadas pelo servidor.
void *atende_cliente(void *arg_slot);
void *thread_menu_qos(void *arg);

int read_file(char *path, char **body, size_t *file_size);
int write_file(char *path, char *body, size_t size,
               int clientfd, size_t total_len);
int test_file(char *path);

ssize_t envia_com_limite_banda(
    int sockfd,
    const void *buf,
    size_t count,
    int rate_kbps
);

ssize_t envia_tudo(
    int sockfd,
    const void *buf,
    size_t count
);

int pega_banda_do_ip(const char *ip);
int eh_html(const char *path);
int taxa_atual_da_conexao(int slot);

void atualiza_taxas_ip_locked(const char *ip);
int banda_total_reservada_locked(void);

int busca_estado_ip_locked(const char *ip, int criar);
void registra_html(const char *ip);
double registra_primeiro_objeto(const char *ip);

void atualiza_banda_estimada(
    const char *ip,
    double banda_kbps
);

void envia_resposta_simples(
    int fd_socket,
    const char *status,
    const char *extra_headers,
    const char *corpo,
    int quer_manter_vivo
);


int main(int argc, char *argv[]) {

    // Primeiro le os parametros passados na execucao do programa.
    if (argc > 1) {

        capacidade_maxima = atoi(argv[1]);

        if (capacidade_maxima <= 0) {
            fprintf(
                stderr,
                "Uso: %s <vazao_maxima_kbps> [arquivo_qos]\n",
                argv[0]
            );

            return -1;
        }
    }

    // Se outro arquivo de QoS foi informado, usa ele.
    if (argc > 2) {
        arquivo_qos = argv[2];
    }


    // Comeca com todas as posicoes das listas zeradas.
    memset(
        lista_clientes,
        0,
        sizeof(lista_clientes)
    );

    memset(
        lista_estados_ip,
        0,
        sizeof(lista_estados_ip)
    );


    int porta_entrada;
    struct sockaddr_in meu_endereco;


    // Cria o socket TCP que vai receber as conexoes.
    porta_entrada = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (porta_entrada == -1) {
        perror("Deu erro no socket");
        return -1;
    }


    // Permite reutilizar a porta logo depois de reiniciar o servidor.
    int opt = 1;

    setsockopt(
        porta_entrada,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );


    // Define o endereco e a porta onde o servidor vai escutar.
    meu_endereco.sin_family = AF_INET;
    meu_endereco.sin_port = htons(MYPORT);
    meu_endereco.sin_addr.s_addr = inet_addr(MYADDR);


    // Associa o socket ao endereco configurado.
    if (
        bind(
            porta_entrada,
            (struct sockaddr*)&meu_endereco,
            sizeof(meu_endereco)
        ) == -1
    ) {
        perror("Erro no bind");

        close(porta_entrada);

        return -1;
    }


    // Coloca o socket no modo de espera por clientes.
    if (listen(porta_entrada, 10) == -1) {

        perror("Erro no listen");

        close(porta_entrada);

        return -1;
    }


    // Cria uma thread separada apenas para mostrar o monitor de QoS.
    pthread_t thread_monitor;

    pthread_create(
        &thread_monitor,
        NULL,
        thread_menu_qos,
        NULL
    );

    // O monitor continua rodando sem precisar de join.
    pthread_detach(thread_monitor);


    printf(
        "Servidor rodando em http://0.0.0.0:%d (Capacidade: %d kbps)\n",
        MYPORT,
        capacidade_maxima
    );

    printf(
        "Arquivo de QoS: %s\n",
        arquivo_qos
    );


    while (1) {

        struct sockaddr_in endereco_cliente;
        socklen_t tamanho_cliente =
            sizeof(endereco_cliente);


        // Espera ate um cliente abrir uma nova conexao TCP.
        int socket_cliente = accept(
            porta_entrada,
            (struct sockaddr*)&endereco_cliente,
            &tamanho_cliente
        );

        if (socket_cliente == -1)
            continue;


        // Converte o endereco recebido para uma string com o IP do cliente.
        char ip_cliente[INET_ADDRSTRLEN];

        inet_ntop(
            AF_INET,
            &(endereco_cliente.sin_addr),
            ip_cliente,
            INET_ADDRSTRLEN
        );


        // Consulta qual taxa foi configurada para esse IP.
        int banda_do_cliente =
            pega_banda_do_ip(ip_cliente);


        /*
         * Inicio do controle de admissao.
         *
         * Como varias threads acessam a lista de clientes,
         * o mutex fica travado enquanto esta parte e executada.
         */
        pthread_mutex_lock(&mutex_lista);


        int vaga_livre = -1;
        int ip_ja_ativo = 0;


        // Procura uma vaga livre e verifica se o IP ja possui conexoes.
        for (int i = 0; i < MAX_CLIENTS; i++) {

            if (
                lista_clientes[i].ativo == 1 &&
                strcmp(
                    lista_clientes[i].ip,
                    ip_cliente
                ) == 0
            ) {

                ip_ja_ativo = 1;

            } else if (
                lista_clientes[i].ativo == 0 &&
                vaga_livre == -1
            ) {

                vaga_livre = i;
            }
        }


        /*
         * Um IP ja ativo nao reserva banda novamente.
         * As novas conexoes apenas dividem a taxa que ja foi reservada.
         */
        int banda_total =
            banda_total_reservada_locked();

        int banda_adicional =
            ip_ja_ativo ? 0 : banda_do_cliente;


        /*
         * Se nao houver banda suficiente ou a lista estiver cheia,
         * o cliente recebe 503 e sua conexao e fechada.
         */
        if (
            banda_total + banda_adicional > capacidade_maxima ||
            vaga_livre == -1
        ) {

            pthread_mutex_unlock(&mutex_lista);

            const char *corpo =
                "Servidor sobrecarregado.";

            char resposta[256];

            snprintf(
                resposta,
                sizeof(resposta),

                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",

                strlen(corpo),
                corpo
            );


            envia_tudo(
                socket_cliente,
                resposta,
                strlen(resposta)
            );


            close(socket_cliente);

            continue;
        }


        // Se a conexao foi aceita, guarda seus dados em uma posicao livre.
        lista_clientes[vaga_livre].ativo = 1;

        lista_clientes[vaga_livre].socket_fd =
            socket_cliente;

        strcpy(
            lista_clientes[vaga_livre].ip,
            ip_cliente
        );

        lista_clientes[vaga_livre].taxa_base_kbps =
            banda_do_cliente;

        lista_clientes[vaga_livre].taxa_kbps =
            banda_do_cliente;

        lista_clientes[vaga_livre].rtt_ms =
            0.0;

        lista_clientes[vaga_livre].banda_estimada_kbps =
            0.0;


        // Depois da nova conexao, divide novamente a taxa entre as conexoes do mesmo IP.
        atualiza_taxas_ip_locked(ip_cliente);


        pthread_mutex_unlock(&mutex_lista);


        /*
         * Cada cliente aceito recebe uma thread propria.
         * A posicao da lista e passada para a thread saber
         * quais dados pertencem a ela.
         */
        int *posicao =
            malloc(sizeof(int));


        if (!posicao) {

            close(socket_cliente);

            pthread_mutex_lock(&mutex_lista);

            lista_clientes[vaga_livre].ativo = 0;

            atualiza_taxas_ip_locked(ip_cliente);

            pthread_mutex_unlock(&mutex_lista);

            continue;
        }


        *posicao = vaga_livre;


        pthread_t thread_cliente;


        if (
            pthread_create(
                &thread_cliente,
                NULL,
                atende_cliente,
                posicao
            ) == 0
        ) {

            pthread_detach(thread_cliente);

        } else {

            close(socket_cliente);

            free(posicao);


            pthread_mutex_lock(&mutex_lista);

            lista_clientes[vaga_livre].ativo = 0;

            atualiza_taxas_ip_locked(ip_cliente);

            pthread_mutex_unlock(&mutex_lista);
        }
    }


    close(porta_entrada);

    return 0;
}


// Mostra no terminal os clientes ativos e os valores atuais de QoS.
void *thread_menu_qos(void *arg) {

    (void)arg;


    while (1) {

        // Atualiza a exibicao a cada 3 segundos.
        sleep(3);


        // Impede que outra thread altere a lista enquanto ela e mostrada.
        pthread_mutex_lock(&mutex_lista);


        int contador = 0;


        printf(
            "\n--- CLIENTES CONECTADOS (QoS Monitor) ---\n"
        );


        for (int i = 0; i < MAX_CLIENTS; i++) {

            if (lista_clientes[i].ativo == 1) {

                /*
                 * RTT e banda estimada sao armazenados pelo IP.
                 * Aqui os valores sao recuperados para mostrar no monitor.
                 */
                int estado =
                    busca_estado_ip_locked(
                        lista_clientes[i].ip,
                        0
                    );


                double rtt =
                    (estado >= 0)
                    ? lista_estados_ip[estado].rtt_ms
                    : 0.0;


                double banda_estimada =
                    (estado >= 0)
                    ? lista_estados_ip[estado].banda_estimada_kbps
                    : 0.0;


                // Atualiza tambem os valores guardados na estrutura da conexao.
                lista_clientes[i].rtt_ms =
                    rtt;

                lista_clientes[i].banda_estimada_kbps =
                    banda_estimada;


                // Formato usado no monitor durante os testes.
                printf(
                    "[%d] IP: %s | Taxa: %d kbps | "
                    "Banda estimada: %.2f kbps | "
                    "RTT estimado: %.2f ms\n",

                    lista_clientes[i].socket_fd,
                    lista_clientes[i].ip,
                    lista_clientes[i].taxa_kbps,
                    banda_estimada,
                    rtt
                );


                contador++;
            }
        }


        if (contador == 0) {

            printf(
                "Nenhum cliente ativo no momento.\n"
            );
        }


        printf(
            "-----------------------------------------\n"
        );


        pthread_mutex_unlock(&mutex_lista);
    }


    return NULL;
}


// Procura o IP no arquivo de QoS.
// Se ele nao estiver cadastrado, retorna a taxa padrao.
int pega_banda_do_ip(const char *ip) {

    FILE *arquivo =
        fopen(
            arquivo_qos,
            "r"
        );


    if (!arquivo) {

        return BANDA_PADRAO;
    }


    char linha[128];
    char ip_arquivo[INET_ADDRSTRLEN];

    int taxa = 0;


    // Le o arquivo linha por linha procurando o IP recebido.
    while (
        fgets(
            linha,
            sizeof(linha),
            arquivo
        )
    ) {

        // Ignora comentarios e linhas vazias.
        if (
            linha[0] == '#' ||
            linha[0] == '\n'
        ) {
            continue;
        }


        if (
            sscanf(
                linha,
                "%15s %d",
                ip_arquivo,
                &taxa
            ) == 2
        ) {

            if (
                strcmp(
                    ip_arquivo,
                    ip
                ) == 0 &&
                taxa > 0
            ) {

                fclose(arquivo);

                return taxa;
            }
        }
    }


    fclose(arquivo);


    // O IP nao apareceu no arquivo.
    return BANDA_PADRAO;
}


// Soma a banda reservada no servidor contando cada IP apenas uma vez.
int banda_total_reservada_locked(void) {

    int soma = 0;
    int quantidade_ips = 0;


    // Guarda os IPs que ja entraram na soma.
    char ips_contados[MAX_CLIENTS][INET_ADDRSTRLEN];


    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (!lista_clientes[i].ativo)
            continue;


        int ja_contado = 0;


        // Verifica se este IP ja apareceu anteriormente.
        for (int j = 0; j < quantidade_ips; j++) {

            if (
                strcmp(
                    ips_contados[j],
                    lista_clientes[i].ip
                ) == 0
            ) {

                ja_contado = 1;

                break;
            }
        }


        // Se for a primeira conexao encontrada desse IP, soma sua taxa base.
        if (!ja_contado) {

            strncpy(
                ips_contados[quantidade_ips],
                lista_clientes[i].ip,
                INET_ADDRSTRLEN - 1
            );


            ips_contados[quantidade_ips]
                         [INET_ADDRSTRLEN - 1] = '\0';


            quantidade_ips++;


            soma +=
                lista_clientes[i].taxa_base_kbps;
        }
    }


    return soma;
}


// Divide a taxa total do IP entre todas as suas conexoes ativas.
void atualiza_taxas_ip_locked(const char *ip) {

    int quantidade = 0;
    int taxa_base = BANDA_PADRAO;


    // Primeiro conta quantas conexoes desse IP estao abertas.
    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (
            lista_clientes[i].ativo &&
            strcmp(
                lista_clientes[i].ip,
                ip
            ) == 0
        ) {

            quantidade++;

            taxa_base =
                lista_clientes[i].taxa_base_kbps;
        }
    }


    if (quantidade == 0)
        return;


    // Calcula quanto cada conexao pode usar.
    int taxa_por_conexao =
        taxa_base / quantidade;


    if (taxa_por_conexao < 1)
        taxa_por_conexao = 1;


    // Aplica a nova taxa em todas as conexoes daquele IP.
    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (
            lista_clientes[i].ativo &&
            strcmp(
                lista_clientes[i].ip,
                ip
            ) == 0
        ) {

            lista_clientes[i].taxa_kbps =
                taxa_por_conexao;
        }
    }
}


// Le a taxa atual da conexao protegendo a lista com o mutex.
int taxa_atual_da_conexao(int slot) {

    int taxa;


    pthread_mutex_lock(&mutex_lista);

    taxa =
        lista_clientes[slot].taxa_kbps;

    pthread_mutex_unlock(&mutex_lista);


    return taxa;
}


// Procura a estrutura usada para guardar as medicoes de um IP.
int busca_estado_ip_locked(
    const char *ip,
    int criar
) {

    int livre = -1;


    for (int i = 0; i < MAX_CLIENTS; i++) {

        // Se ja existe uma entrada para esse IP, retorna sua posicao.
        if (
            lista_estados_ip[i].usado &&
            strcmp(
                lista_estados_ip[i].ip,
                ip
            ) == 0
        ) {

            return i;
        }


        // Guarda a primeira posicao livre encontrada.
        if (
            !lista_estados_ip[i].usado &&
            livre == -1
        ) {

            livre = i;
        }
    }


    /*
     * Se o IP ainda nao existe e criar == 1,
     * prepara uma nova posicao para ele.
     */
    if (
        criar &&
        livre >= 0
    ) {

        lista_estados_ip[livre].usado = 1;


        strcpy(
            lista_estados_ip[livre].ip,
            ip
        );


        lista_estados_ip[livre]
            .aguardando_primeiro_objeto = 0;


        lista_estados_ip[livre].rtt_ms =
            0.0;


        lista_estados_ip[livre]
            .banda_estimada_kbps = 0.0;


        return livre;
    }


    return -1;
}


// Quando chega um HTML, salva o momento de inicio da medicao do RTT.
void registra_html(const char *ip) {

    pthread_mutex_lock(&mutex_lista);


    int pos =
        busca_estado_ip_locked(
            ip,
            1
        );


    if (pos >= 0) {

        gettimeofday(
            &lista_estados_ip[pos].instante_html,
            NULL
        );


        lista_estados_ip[pos]
            .aguardando_primeiro_objeto = 1;
    }


    pthread_mutex_unlock(&mutex_lista);
}


// Calcula o RTT entre o pedido do HTML e o primeiro objeto seguinte.
double registra_primeiro_objeto(const char *ip) {

    double rtt = 0.0;


    struct timeval agora;


    gettimeofday(
        &agora,
        NULL
    );


    pthread_mutex_lock(&mutex_lista);


    int pos =
        busca_estado_ip_locked(
            ip,
            1
        );


    // So calcula se antes houve uma requisicao HTML.
    if (
        pos >= 0 &&
        lista_estados_ip[pos]
            .aguardando_primeiro_objeto
    ) {

        rtt =
            (
                agora.tv_sec -
                lista_estados_ip[pos]
                    .instante_html.tv_sec
            ) * 1000.0;


        rtt +=
            (
                agora.tv_usec -
                lista_estados_ip[pos]
                    .instante_html.tv_usec
            ) / 1000.0;


        lista_estados_ip[pos].rtt_ms =
            rtt;


        /*
         * Apenas o primeiro objeto depois do HTML
         * deve participar desta medicao.
         */
        lista_estados_ip[pos]
            .aguardando_primeiro_objeto = 0;
    }


    pthread_mutex_unlock(&mutex_lista);


    return rtt;
}


// Salva a ultima largura de banda calculada para esse IP.
void atualiza_banda_estimada(
    const char *ip,
    double banda_kbps
) {

    pthread_mutex_lock(&mutex_lista);


    int pos =
        busca_estado_ip_locked(
            ip,
            1
        );


    if (
        pos >= 0 &&
        banda_kbps > 0.0
    ) {

        lista_estados_ip[pos]
            .banda_estimada_kbps =
            banda_kbps;
    }


    pthread_mutex_unlock(&mutex_lista);
}


// Verifica se o arquivo pedido e HTML.
int eh_html(const char *path) {

    if (!path)
        return 0;


    const char *ext =
        strrchr(
            path,
            '.'
        );


    if (!ext)
        return 0;


    return
        strcasecmp(
            ext,
            ".html"
        ) == 0
        ||
        strcasecmp(
            ext,
            ".htm"
        ) == 0;
}


// Envia todo o buffer.
// Caso write() envie apenas uma parte, continua do ponto onde parou.
ssize_t envia_tudo(
    int socket_cliente,
    const void *conteudo,
    size_t tamanho
) {

    size_t ja_enviado = 0;


    const char *dados =
        (const char *)conteudo;


    while (ja_enviado < tamanho) {

        ssize_t enviado =
            write(
                socket_cliente,
                dados + ja_enviado,
                tamanho - ja_enviado
            );


        if (enviado <= 0)
            return enviado;


        ja_enviado +=
            (size_t)enviado;
    }


    return
        (ssize_t)ja_enviado;
}


// Envia um objeto em blocos e usa pausas para respeitar a taxa configurada.
ssize_t envia_com_limite_banda(
    int socket_cliente,
    const void *conteudo,
    size_t tamanho,
    int taxa_kbps
) {

    if (taxa_kbps <= 0)
        taxa_kbps = BANDA_PADRAO;


    // Converte kilobits por segundo para bytes por segundo.
    double bytes_por_segundo =
        (taxa_kbps * 1000.0) / 8.0;


    size_t ja_enviado = 0;


    const char *ponteiro_dados =
        (const char *)conteudo;


    // Continua ate terminar todo o conteudo.
    while (ja_enviado < tamanho) {

        size_t pedaco =
            tamanho - ja_enviado;


        // Cada write envia no maximo 4 KB.
        if (pedaco > BUFFER_SIZE)
            pedaco = BUFFER_SIZE;


        ssize_t sucesso_envio =
            write(
                socket_cliente,
                ponteiro_dados + ja_enviado,
                pedaco
            );


        if (sucesso_envio <= 0)
            break;


        ja_enviado +=
            (size_t)sucesso_envio;


        /*
         * Calcula quanto tempo esse bloco deveria levar
         * na taxa definida e faz uma pequena pausa.
         */
        double tempo_esperado =
            (double)sucesso_envio /
            bytes_por_segundo;


        usleep(
            (useconds_t)(
                tempo_esperado *
                1000000.0
            )
        );
    }


    return
        (ssize_t)ja_enviado;
}


// Monta respostas HTTP pequenas como erros, PUT e OPTIONS.
void envia_resposta_simples(
    int fd_socket,
    const char *status,
    const char *extra_headers,
    const char *corpo,
    int quer_manter_vivo
) {

    if (!corpo)
        corpo = "";


    if (!extra_headers)
        extra_headers = "";


    char header[512];


    // Content-Length informa exatamente o tamanho do corpo enviado.
    snprintf(
        header,
        sizeof(header),

        "HTTP/1.1 %s\r\n"
        "%s"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",

        status,
        extra_headers,
        strlen(corpo),

        quer_manter_vivo
            ? "keep-alive"
            : "close"
    );


    envia_tudo(
        fd_socket,
        header,
        strlen(header)
    );


    // O corpo so e enviado quando ele existe.
    if (strlen(corpo) > 0) {

        envia_tudo(
            fd_socket,
            corpo,
            strlen(corpo)
        );
    }
}


// Cada cliente aceito pelo servidor executa esta funcao em uma thread.
void *atende_cliente(void *arg_slot) {

    // Recupera qual posicao da lista pertence a esta thread.
    int minha_vaga =
        *(int*)arg_slot;


    free(arg_slot);


    int fd_socket;

    char meu_ip[INET_ADDRSTRLEN];


    // Copia os dados da conexao antes de comecar o atendimento.
    pthread_mutex_lock(&mutex_lista);


    fd_socket =
        lista_clientes[minha_vaga]
            .socket_fd;


    strcpy(
        meu_ip,
        lista_clientes[minha_vaga].ip
    );


    pthread_mutex_unlock(&mutex_lista);


    char buffer_leitura[
        BUFFER_SIZE + 1
    ];


    /*
     * O while mantem a conexao aberta.
     * Assim o mesmo cliente pode enviar varias requisicoes
     * usando o mesmo socket HTTP/1.1.
     */
    while (1) {

        // Aguarda uma nova requisicao do cliente.
        ssize_t bytes_lidos =
            read(
                fd_socket,
                buffer_leitura,
                BUFFER_SIZE
            );


        // read <= 0 significa que a conexao terminou ou ocorreu erro.
        if (bytes_lidos <= 0)
            break;


        // Coloca '\0' apenas para facilitar o tratamento como string.
        buffer_leitura[bytes_lidos] =
            '\0';


        // O parser transforma o texto HTTP em uma estrutura mais simples.
        struct message *mensagem_http =
            parse_message(
                buffer_leitura,
                (size_t)bytes_lidos
            );


        // Se a requisicao nao puder ser interpretada, retorna 400.
        if (
            mensagem_http == NULL ||
            mensagem_http->start_line == NULL
        ) {

            envia_resposta_simples(
                fd_socket,
                "400 Bad Request",
                NULL,
                "",
                0
            );


            if (mensagem_http)
                free_message(mensagem_http);


            break;
        }


        size_t tamanho_conteudo =
            (size_t)-1;


        // No HTTP/1.1 a conexao fica aberta por padrao.
        int quer_manter_vivo = 1;


        /*
         * Procura os headers importantes para esta implementacao:
         * Content-Length e Connection.
         */
        for (
            int i = 0;
            i < 8 &&
            mensagem_http->field_lines[i].name != NULL;
            i++
        ) {

            if (
                strcasecmp(
                    mensagem_http
                        ->field_lines[i].name,
                    "Content-Length"
                ) == 0
            ) {

                tamanho_conteudo =
                    (size_t)strtoul(
                        mensagem_http
                            ->field_lines[i].value,
                        NULL,
                        10
                    );
            }


            if (
                strcasecmp(
                    mensagem_http
                        ->field_lines[i].name,
                    "Connection"
                ) == 0
            ) {

                if (
                    strcasestr(
                        mensagem_http
                            ->field_lines[i].value,
                        "close"
                    )
                ) {

                    quer_manter_vivo = 0;
                }
            }
        }


        /*
         * O caminho HTTP vem como /arquivo.
         * Para abrir no sistema de arquivos, a barra inicial e removida.
         */
        char *caminho_arquivo =
            mensagem_http
                ->start_line
                ->request
                .target;


        if (
            caminho_arquivo != NULL &&
            caminho_arquivo[0] == '/'
        ) {

            caminho_arquivo++;
        }


        // Uma requisicao "/" abre o index.html.
        if (
            caminho_arquivo &&
            strlen(caminho_arquivo) == 0
        ) {

            caminho_arquivo =
                "index.html";
        }


        /*
         * A partir daqui o servidor executa
         * uma acao diferente para cada metodo HTTP.
         */
        switch (
            mensagem_http
                ->start_line
                ->request
                .method
        ) {


            case GET: {

                char *texto_arquivo = NULL;
                size_t tamanho_arq = 0;


                // Tenta carregar o recurso solicitado.
                int status_leitura =
                    read_file(
                        caminho_arquivo,
                        &texto_arquivo,
                        &tamanho_arq
                    );


                if (status_leitura == 0) {

                    int arquivo_html =
                        eh_html(
                            caminho_arquivo
                        );


                    /*
                     * O pedido do HTML marca o primeiro instante.
                     * O primeiro objeto seguinte calcula o RTT.
                     */
                    if (arquivo_html) {

                        registra_html(meu_ip);

                    } else {

                        registra_primeiro_objeto(
                            meu_ip
                        );
                    }


                    char header_resposta[256];


                    snprintf(
                        header_resposta,
                        sizeof(header_resposta),

                        "HTTP/1.1 200 OK\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: %s\r\n"
                        "\r\n",

                        tamanho_arq,

                        quer_manter_vivo
                            ? "keep-alive"
                            : "close"
                    );


                    // O header sempre e enviado normalmente.
                    envia_tudo(
                        fd_socket,
                        header_resposta,
                        strlen(header_resposta)
                    );


                    if (arquivo_html) {

                        /*
                         * HTML e a excecao do trabalho:
                         * ele nao sofre limitacao de taxa.
                         */
                        envia_tudo(
                            fd_socket,
                            texto_arquivo,
                            tamanho_arq
                        );

                    } else {

                        /*
                         * Para outros objetos, consulta a taxa atual
                         * da conexao e aplica o throttling.
                         */
                        int taxa =
                            taxa_atual_da_conexao(
                                minha_vaga
                            );


                        struct timeval
                            inicio_envio,
                            fim_envio;


                        gettimeofday(
                            &inicio_envio,
                            NULL
                        );


                        ssize_t enviados =
                            envia_com_limite_banda(
                                fd_socket,
                                texto_arquivo,
                                tamanho_arq,
                                taxa
                            );


                        gettimeofday(
                            &fim_envio,
                            NULL
                        );


                        /*
                         * Mede quanto tempo levou o envio
                         * para estimar a banda observada.
                         */
                        double tempo_ms =
                            (
                                fim_envio.tv_sec -
                                inicio_envio.tv_sec
                            ) * 1000.0;


                        tempo_ms +=
                            (
                                fim_envio.tv_usec -
                                inicio_envio.tv_usec
                            ) / 1000.0;


                        if (
                            enviados > 0 &&
                            tempo_ms > 0.0
                        ) {

                            double banda_estimada =
                                (
                                    (double)enviados *
                                    8.0
                                ) /
                                tempo_ms;


                            /*
                             * bits/ms possui o mesmo valor
                             * numerico de kbps.
                             */
                            atualiza_banda_estimada(
                                meu_ip,
                                banda_estimada
                            );
                        }
                    }


                    free(texto_arquivo);


                } else if (
                    status_leitura == 1
                ) {

                    // O recurso nao existe.
                    envia_resposta_simples(
                        fd_socket,
                        "404 Not Found",
                        "Content-Type: text/html\r\n",
                        "<h1>erro 404</h1>",
                        quer_manter_vivo
                    );


                } else {

                    envia_resposta_simples(
                        fd_socket,
                        "500 Internal Server Error",
                        "Content-Type: text/html\r\n",
                        "<h1>Erro interno</h1>",
                        quer_manter_vivo
                    );
                }

            } break;


            case HEAD: {

                char *texto_arquivo = NULL;
                size_t tamanho_arq = 0;


                /*
                 * O arquivo e aberto para descobrir seu tamanho,
                 * mas o corpo nao sera enviado.
                 */
                int status_leitura =
                    read_file(
                        caminho_arquivo,
                        &texto_arquivo,
                        &tamanho_arq
                    );


                if (status_leitura == 0) {

                    char header_resposta[256];


                    snprintf(
                        header_resposta,
                        sizeof(header_resposta),

                        "HTTP/1.1 200 OK\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: %s\r\n"
                        "\r\n",

                        tamanho_arq,

                        quer_manter_vivo
                            ? "keep-alive"
                            : "close"
                    );


                    envia_tudo(
                        fd_socket,
                        header_resposta,
                        strlen(header_resposta)
                    );


                    // HEAD nunca envia o corpo do arquivo.
                    free(texto_arquivo);


                } else {

                    char header_resposta[256];


                    snprintf(
                        header_resposta,
                        sizeof(header_resposta),

                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: %s\r\n"
                        "\r\n",

                        quer_manter_vivo
                            ? "keep-alive"
                            : "close"
                    );


                    envia_tudo(
                        fd_socket,
                        header_resposta,
                        strlen(header_resposta)
                    );
                }

            } break;


            case PUT: {

                /*
                 * O PUT grava o corpo recebido
                 * no caminho informado pelo cliente.
                 */
                if (
                    write_file(
                        caminho_arquivo,

                        mensagem_http->body
                            ? mensagem_http->body
                            : "",

                        mensagem_http->body_size,

                        fd_socket,

                        tamanho_conteudo
                    ) == 0
                ) {

                    envia_resposta_simples(
                        fd_socket,
                        "200 OK",
                        "Content-Type: text/html\r\n",
                        "<h1>Gravou com sucesso</h1>",
                        quer_manter_vivo
                    );

                } else {

                    envia_resposta_simples(
                        fd_socket,
                        "403 Forbidden",
                        "Content-Type: text/html\r\n",
                        "<h1>Deu ruim na gravacao</h1>",
                        quer_manter_vivo
                    );
                }

            } break;


            case OPTIONS: {

                // Informa quais metodos o servidor aceita.
                envia_resposta_simples(
                    fd_socket,
                    "204 No Content",

                    "Allow: OPTIONS, GET, HEAD, PUT, TRACE\r\n",

                    "",
                    quer_manter_vivo
                );

            } break;


            case TRACE: {

                /*
                 * TRACE devolve ao cliente
                 * exatamente a requisicao recebida.
                 */
                char header_resposta[256];


                snprintf(
                    header_resposta,
                    sizeof(header_resposta),

                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: message/http\r\n"
                    "Content-Length: %zd\r\n"
                    "Connection: %s\r\n"
                    "\r\n",

                    bytes_lidos,

                    quer_manter_vivo
                        ? "keep-alive"
                        : "close"
                );


                envia_tudo(
                    fd_socket,
                    header_resposta,
                    strlen(header_resposta)
                );


                envia_tudo(
                    fd_socket,
                    buffer_leitura,
                    (size_t)bytes_lidos
                );

            } break;


            default: {

                // Qualquer metodo nao tratado recebe 405.
                envia_resposta_simples(
                    fd_socket,
                    "405 Method Not Allowed",

                    "Allow: OPTIONS, GET, HEAD, PUT, TRACE\r\n"
                    "Content-Type: text/html\r\n",

                    "<h1>Metodo nao suportado</h1>",

                    quer_manter_vivo
                );

            } break;
        }


        // Libera toda a memoria criada pelo parser.
        free_message(mensagem_http);


        // Se o cliente pediu Connection: close, encerra o while.
        if (quer_manter_vivo == 0) {
            break;
        }
    }


    /*
     * A conexao terminou.
     * A vaga fica livre e as conexoes restantes
     * do mesmo IP recebem uma nova divisao de taxa.
     */
    pthread_mutex_lock(&mutex_lista);


    lista_clientes[minha_vaga].ativo =
        0;


    atualiza_taxas_ip_locked(
        meu_ip
    );


    pthread_mutex_unlock(&mutex_lista);


    shutdown(
        fd_socket,
        SHUT_RDWR
    );


    close(fd_socket);


    pthread_exit(NULL);
}


// Le um arquivo inteiro em modo binario.
int read_file(
    char *path,
    char **body,
    size_t *file_size
) {

    if (
        !path ||
        strlen(path) == 0
    ) {

        return 1;
    }


    // "rb" permite ler tanto texto quanto arquivos binarios.
    FILE* file =
        fopen(
            path,
            "rb"
        );


    if (!file)
        return 1;


    // Vai ate o final para descobrir o tamanho do arquivo.
    fseek(
        file,
        0,
        SEEK_END
    );


    long size =
        ftell(file);


    // Volta ao inicio antes da leitura.
    fseek(
        file,
        0,
        SEEK_SET
    );


    if (size < 0) {

        fclose(file);

        return 1;
    }


    // Reserva memoria suficiente para guardar todo o arquivo.
    *body =
        (char*)malloc(
            (size_t)size + 1
        );


    if (!*body) {

        fclose(file);

        return 1;
    }


    size_t read_bytes =
        fread(
            *body,
            1,
            (size_t)size,
            file
        );


    // O zero final facilita quando o arquivo e tratado como texto.
    (*body)[read_bytes] =
        '\0';


    *file_size =
        read_bytes;


    fclose(file);


    return 0;
}


// Grava o corpo recebido em uma requisicao PUT.
int write_file(
    char *path,
    char *body,
    size_t size,
    int clientfd,
    size_t total_len
) {

    if (
        !path ||
        strlen(path) == 0
    ) {

        return 1;
    }


    FILE* file =
        fopen(
            path,
            "wb"
        );


    if (!file)
        return 1;


    /*
     * A primeira parte do corpo pode ja ter vindo
     * junto com os headers no primeiro read().
     */
    size_t inicial =
        size;


    /*
     * Nunca grava alem do tamanho informado
     * pelo Content-Length.
     */
    if (
        total_len != (size_t)-1 &&
        inicial > total_len
    ) {

        inicial =
            total_len;
    }


    // Grava primeiro os bytes que ja estavam no parser.
    if (
        body &&
        inicial > 0
    ) {

        if (
            fwrite(
                body,
                1,
                inicial,
                file
            ) != inicial
        ) {

            fclose(file);

            return 1;
        }
    }


    size_t recebido =
        inicial;


    /*
     * Se ainda faltarem bytes do corpo,
     * continua lendo diretamente do socket.
     */
    if (
        total_len != (size_t)-1
    ) {

        while (
            total_len > recebido
        ) {

            char temp_buffer[
                BUFFER_SIZE
            ];


            size_t faltam =
                total_len - recebido;


            size_t pedir =
                faltam < BUFFER_SIZE
                ? faltam
                : BUFFER_SIZE;


            ssize_t tmp_size =
                read(
                    clientfd,
                    temp_buffer,
                    pedir
                );


            if (tmp_size <= 0) {

                fclose(file);

                return 1;
            }


            if (
                fwrite(
                    temp_buffer,
                    1,
                    (size_t)tmp_size,
                    file
                ) !=
                (size_t)tmp_size
            ) {

                fclose(file);

                return 1;
            }


            recebido +=
                (size_t)tmp_size;
        }
    }


    fclose(file);


    return 0;
}


// Verifica apenas se um arquivo pode ser aberto.
int test_file(char *path) {

    if (
        !path ||
        strlen(path) == 0
    ) {

        return 1;
    }


    FILE* file =
        fopen(
            path,
            "rb"
        );


    if (!file)
        return 1;


    fclose(file);


    return 0;
}