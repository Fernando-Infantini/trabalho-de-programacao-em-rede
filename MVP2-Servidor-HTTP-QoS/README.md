# MVP 2 - Servidor HTTP/1.1 com QoS

## Integrantes

- Fernando Infantini
- Ezequiel Alves
- Ronaldy Gelos

## Descrição

Esta pasta contém o código-fonte da segunda versão do trabalho da disciplina de Fundamentos e Avaliação de Redes de Computadores.

O projeto implementa um servidor HTTP/1.1 concorrente em linguagem C, utilizando Sockets POSIX e Pthreads. Nesta versão foram incorporados os mecanismos de Qualidade de Serviço (QoS) previstos no trabalho, mantendo as conexões persistentes e o atendimento simultâneo de clientes.

Entre as funcionalidades desta versão estão:

- atendimento concorrente com Pthreads;
- conexões HTTP/1.1 persistentes (Keep-Alive);
- métodos `GET`, `PUT`, `HEAD`, `OPTIONS` e `TRACE`;
- leitura de arquivo auxiliar com mapeamento `IP -> taxa máxima`;
- taxa padrão de 1000 kbps para IPs não cadastrados;
- controle da taxa de envio dos objetos que não são HTML;
- divisão da taxa entre conexões simultâneas do mesmo endereço IP;
- controle de admissão considerando a vazão máxima do servidor;
- estimativa de RTT pelo intervalo entre a requisição do HTML e a primeira requisição de objeto seguinte do mesmo cliente;
- estimativa da largura de banda observada durante o envio do objeto;
- monitor em tempo real dos clientes atendidos, taxa, banda estimada e RTT, apresentado em tabela no terminal;
- resumo da capacidade de banda reservada e do número de clientes ativos.

O servidor escuta em todas as interfaces de rede (`0.0.0.0`) na porta `9998`, permitindo testes locais e também a partir de outros hosts da rede.

## Arquivo de configuração de QoS

O arquivo `qos_config.txt` define a taxa máxima associada a cada endereço IP.

Formato:

```text
IP TAXA_MAXIMA_KBPS
```

Exemplo:

```text
127.0.0.1 1000
192.168.0.10 500
192.168.0.20 2000
```

Caso o IP de um cliente não esteja presente no arquivo, será utilizada a taxa padrão de `1000 kbps`.

Se um mesmo IP abrir várias conexões simultâneas, a taxa definida para esse IP é dividida igualmente entre elas.

O cálculo da banda total reservada considera cada endereço IP ativo uma única vez, independentemente da posição ocupada pelas conexões na lista interna de clientes.

## Compilação

O projeto deve ser compilado em ambiente Linux ou WSL, com GCC, Make e Pthreads disponíveis.

Dentro da pasta do projeto, execute:

```bash
make
```

Será criado o executável:

```text
servidor
```

Para remover os arquivos gerados pela compilação:

```bash
make clear
```

## Execução

A execução pode ser feita informando somente a vazão máxima do servidor:

```bash
./servidor 5000
```

Nesse caso será utilizado automaticamente o arquivo:

```text
qos_config.txt
```

Também é possível informar outro arquivo de configuração:

```bash
./servidor 5000 meu_qos.txt
```

Onde `5000` representa a vazão máxima total do servidor em kbps.

## Exemplos de teste

GET:

```bash
curl -i http://127.0.0.1:9998/arquivo.txt
```

HEAD:

```bash
curl -I http://127.0.0.1:9998/arquivo.txt
```

PUT:

```bash
curl -i -X PUT http://127.0.0.1:9998/teste.txt -d "abc"
```

OPTIONS:

```bash
curl -i -X OPTIONS http://127.0.0.1:9998/
```

TRACE:

```bash
curl -i -X TRACE http://127.0.0.1:9998/
```

Para observar a estimativa de RTT prevista no trabalho, primeiro deve ser solicitado um arquivo HTML e, em seguida, o primeiro objeto referenciado pela página. O monitor do servidor utiliza o intervalo entre essas duas requisições consecutivas do mesmo IP como estimativa do RTT.


## Página de teste com múltiplas imagens

O arquivo `index.html` foi adicionado para os testes de concorrência e QoS. Ele referencia cinco imagens BMP de aproximadamente 6 MB cada.

As imagens já estão incluídas nesta pasta. Caso seja necessário recriá-las, execute:

```bash
python3 gerar_imagens.py
```

Depois, com o servidor em execução, acesse:

```text
http://127.0.0.1:9998/index.html
```

O HTML é enviado sem limitação de taxa. As imagens são tratadas como objetos não HTML e passam pelo controle de banda. Isso permite observar no monitor a divisão de taxa, o RTT estimado e a largura de banda durante as transferências.

## Autoria e uso de ferramentas externas

**Este projeto foi desenvolvido integralmente pela equipe, sem ajuda não autorizada de alunos não membros do projeto no processo de codificação.**

O código-base da primeira versão foi produzido pela equipe. Nesta segunda versão, seguimos a mesma ideia de implementação do MVP1, usando a Inteligência Artificial **ChatGPT, da OpenAI**,  especificamente para colocar comentarios e formatar o código.

