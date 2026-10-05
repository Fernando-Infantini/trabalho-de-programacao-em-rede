# Servidor HTTP com Qualidade de Serviço (QoS)

## Sobre o Projeto

Este repositório contém o desenvolvimento do trabalho prático da disciplina de **Fundamentos e Avaliação de Redes de Computadores – 2026/2**.

O trabalho tem como objetivo desenvolver e avaliar um **servidor HTTP com mecanismos de Qualidade de Serviço (QoS)**, permitindo aplicar na prática conceitos relacionados à programação em redes, protocolos de comunicação, concorrência, desempenho e avaliação de redes de computadores.

O servidor é desenvolvido em **linguagem C**, para ambiente **Linux**, utilizando **Sockets TCP** para comunicação e **Pthreads** para permitir o atendimento concorrente de diferentes clientes.

O desenvolvimento é realizado de maneira incremental, dividido em diferentes versões (MVPs), nas quais novas funcionalidades são incorporadas ao servidor ao longo do projeto.

## Objetivo Geral

O objetivo geral do trabalho é compreender, por meio de atividades práticas de programação e avaliação, conceitos e elementos presentes em redes de computadores modernas.

Para isso, o projeto envolve tanto a implementação de um servidor HTTP quanto a realização de experimentos para observar e avaliar seu funcionamento e desempenho em diferentes situações.

## Objetivos Específicos

Entre os principais objetivos do trabalho estão:

- compreender o funcionamento da comunicação cliente-servidor;
- aplicar programação em rede utilizando sockets TCP;
- compreender e implementar aspectos do protocolo HTTP/1.1;
- permitir o atendimento concorrente de múltiplos clientes;
- trabalhar com conexões HTTP persistentes;
- estudar os efeitos de diferentes condições de rede sobre o desempenho das aplicações;
- analisar características como atraso, largura de banda e vazão;
- implementar mecanismos de controle de taxa de transmissão;
- aplicar conceitos de Qualidade de Serviço (QoS);
- realizar controle de recursos de acordo com a origem das conexões;
- realizar medições e avaliações do comportamento do servidor;
- utilizar ferramentas de análise de tráfego e desempenho de redes.

## Servidor HTTP

O sistema desenvolvido consiste em um servidor capaz de receber conexões de clientes através do protocolo TCP e processar requisições HTTP.

De forma geral, seu funcionamento pode ser representado da seguinte maneira:

```text
Cliente
   |
   | Conexão TCP
   v
Servidor HTTP
   |
   | Recebe a requisição
   v
Interpretação da mensagem HTTP
   |
   v
Processamento da requisição
   |
   v
Controle e gerenciamento dos recursos
   |
   v
Construção da resposta HTTP
   |
   v
Resposta enviada ao cliente
```

O uso de **Pthreads** permite que diferentes conexões sejam tratadas de maneira concorrente, possibilitando que o servidor atenda múltiplos clientes.

## Qualidade de Serviço (QoS)

Ao longo do desenvolvimento são incorporados mecanismos relacionados à Qualidade de Serviço.

O servidor deverá ser capaz de considerar características da conexão e do cliente para controlar a utilização dos recursos disponíveis.

Entre os aspectos trabalhados estão:

- controle da taxa de envio de dados;
- definição de limites de transmissão de acordo com o endereço IP do cliente;
- compartilhamento da taxa disponível entre conexões de uma mesma origem;
- estimativa do atraso de comunicação;
- estimativa da largura de banda;
- acompanhamento dos clientes atendidos;
- controle da vazão total disponibilizada pelo servidor;
- mecanismos de controle de admissão.

Esses recursos permitem observar como decisões tomadas pelo servidor podem influenciar o desempenho percebido pelas diferentes aplicações e clientes.

## Avaliação de Desempenho

Além da implementação, o trabalho também envolve a avaliação experimental do servidor.

São realizados testes utilizando múltiplos clientes e páginas contendo diferentes objetos, permitindo observar o comportamento do servidor em situações de maior utilização da rede.

Entre as ferramentas que podem ser utilizadas durante os experimentos estão:

- `curl`;
- `wget`;
- Wireshark;
- TCPdump;
- IPTraf.

Essas ferramentas permitem gerar requisições, capturar pacotes e observar informações relacionadas ao funcionamento e ao desempenho da comunicação.

Sempre que possível, também podem ser analisados diferentes meios de acesso à rede, como **Ethernet e Wi-Fi**, permitindo observar como características da tecnologia de transmissão podem afetar o comportamento das aplicações.

## Desenvolvimento Incremental

O projeto é desenvolvido em **três versões (MVPs)**.

Cada versão representa uma etapa da evolução do servidor, permitindo que as funcionalidades sejam implementadas e avaliadas progressivamente.

O repositório é organizado de forma que o código-fonte correspondente a cada versão seja mantido separadamente.

Uma estrutura possível é:

```text
servidor-http-qos/
│
├── README.md
│
├── MVP1/
│   ├── código-fonte
│   └── README.md
│
├── MVP2/
│   ├── código-fonte
│   └── README.md
│
└── MVP3/
    ├── código-fonte
    └── README.md
```

Cada pasta possui seu próprio `README.md`, contendo informações específicas sobre compilação, execução e funcionalidades daquela versão.

## Tecnologias e Ferramentas

O projeto utiliza principalmente:

- **C** para implementação do servidor;
- **Sockets POSIX** para programação em rede;
- **TCP/IP** para comunicação;
- **HTTP/1.1** como protocolo de aplicação;
- **Pthreads** para concorrência;
- **GCC** para compilação;
- **Make** para automatização da compilação;
- **Linux / WSL** como ambiente de desenvolvimento e execução;
- **Git e GitHub** para controle de versão;
- ferramentas de análise de tráfego e desempenho de redes.

## Resultado Esperado

Ao final do trabalho, espera-se obter um servidor HTTP capaz de atender múltiplos clientes, controlar os recursos de rede disponíveis e aplicar mecanismos de Qualidade de Serviço.

Além da implementação do software, o trabalho busca proporcionar uma compreensão prática da relação entre protocolos, programação em rede, tecnologias de transmissão e desempenho das aplicações.
