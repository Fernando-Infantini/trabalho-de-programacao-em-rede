# MVP 1 - Servidor HTTP Básico

## Integrantes

- Fernando Infantini
- Ezequiel Alves
- Ronaldy Gelos

## Descrição

Esta pasta contém o código-fonte referente ao MVP 1 do trabalho da disciplina de Fundamentos e Avaliação de Redes de Computadores.

O projeto consiste na implementação de um servidor HTTP/1.1 concorrente desenvolvido em linguagem C, utilizando Sockets POSIX para comunicação TCP e Pthreads para o atendimento concorrente de clientes.

O servidor opera localmente no endereço:

```text
127.0.0.1:9998
```

Nesta versão foram implementados o recebimento e a interpretação de requisições HTTP, o envio de arquivos e respostas ao cliente e o tratamento dos métodos HTTP previstos no projeto.

Os principais arquivos do projeto são:

- `main.c`: implementação do servidor, sockets, conexões e processamento das requisições;
- `message.c` e `message.h`: estruturas e funções utilizadas para representar mensagens HTTP;
- `parser.c` e `parser.h`: interpretação das requisições HTTP recebidas;
- `Makefile`: automatiza o processo de compilação do servidor.

## Compilação

O projeto deve ser compilado em ambiente Linux ou através do WSL (Windows Subsystem for Linux).

É necessário possuir o GCC, Make e suporte à biblioteca Pthreads instalados.

Para compilar o servidor, execute dentro da pasta do projeto:

```bash
make
```

Após a compilação será criado o executável:

```text
servidor
```

## Execução

Para iniciar o servidor, execute:

```bash
./servidor
```

O servidor será iniciado no endereço:

```text
http://127.0.0.1:9998
```

Para interromper sua execução, utilize:

```text
Ctrl + C
```

Para remover os arquivos gerados durante a compilação:

```bash
make clear
```

## Testes

Os testes podem ser realizados utilizando o comando `curl` em outro terminal.

Exemplo de requisição GET:

```bash
curl -i http://127.0.0.1:9998/main.c
```

Exemplo de requisição OPTIONS:

```bash
curl -i -X OPTIONS http://127.0.0.1:9998/
```

Exemplo de requisição PUT:

```bash
curl -i -X PUT http://127.0.0.1:9998/teste.txt -d "Conteudo enviado pelo PUT"
```

Também é possível realizar testes através de um navegador acessando, por exemplo:

```text
http://127.0.0.1:9998/main.c
```

## Funcionalidades desta versão

Nesta versão do servidor foram desenvolvidas funcionalidades relacionadas a:

- criação e configuração de sockets TCP;
- uso de `bind()`, `listen()` e `accept()`;
- atendimento concorrente de clientes utilizando Pthreads;
- interpretação de mensagens HTTP;
- métodos `GET`, `PUT`, `HEAD`, `OPTIONS` e `TRACE`;
- envio de arquivos ao cliente;
- tratamento de recursos inexistentes com resposta `404 Not Found`;
- gerenciamento e liberação da memória utilizada pelas mensagens HTTP.

## Observações

O servidor foi desenvolvido para execução em ambiente Linux. Durante os testes também foi utilizado o WSL no Windows para compilação e execução.

O projeto utiliza a biblioteca Pthreads para permitir que diferentes clientes sejam atendidos de forma concorrente.

Os testes desta versão podem ser realizados tanto pelo terminal, através do `curl`, quanto por navegadores web.

## Autoria e uso de ferramentas externas

**Este projeto foi desenvolvido integralmente pela equipe, sem ajuda não autorizada de alunos não membros do projeto no processo de codificação.**

Não foram utilizados trechos de código disponíveis na Internet nem códigos produzidos com apoio de ferramentas de Inteligência Artificial durante o desenvolvimento do código-fonte desta versão.

Ferramentas de Inteligência Artificial foram utilizadas posteriormente apenas como auxílio na organização e revisão da documentação do projeto, não na implementação do código-fonte.
