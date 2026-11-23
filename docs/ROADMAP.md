# Roadmap funcional oficial — Goodix GXFP5A8B

Este documento restaura o planejamento funcional original. Os números abaixo
representam capacidades do driver Linux no dispositivo real. Nomes históricos
de ZIPs, diretórios e suítes de teste não comprovam conclusão desses marcos.
O estado de execução está em [CURRENT_STATUS.md](CURRENT_STATUS.md).

| Versão | Funcionalidade | Objetivo original |
|---|---|---|
| 0.1 | Associação ACPI/SPI passiva | Confirmar reconhecimento pelo driver |
| 0.2 | Inspeção de GPIO e IRQ | Validar recursos do sensor |
| 0.3 | Transporte SPI | Implementar leitura e escrita de pacotes |
| 0.4 | Recepção de interrupções | Processar respostas do microcontrolador |
| 0.5 | Consulta controlada de Chip ID | Confirmar comunicação com hardware |
| 0.6 | Inicialização segura | Preparar o dispositivo sem alterar firmware |
| 0.7 | Protocolo biométrico | Investigar cadastro e verificação |
| 1.0 | Integração com libfprint | Utilizar a digital no Linux |

## Regra de aceitação

Cada marco deve ter fonte identificado por hash, ambiente e dispositivo
identificados, evidência da capacidade esperada e resultado revisável. Compilar,
registrar um driver, obter sucesso do controlador ou passar testes offline são
evidências distintas. Marcos posteriores continuam dependentes dos anteriores.

| Marco | Evidência mínima para fechamento funcional |
|---|---|
| 0.1 | Associação ao cliente ACPI GXFP5A8B correto, metadados SPI e vínculo do driver registrados, sem I/O ativo pelo módulo |
| 0.2 | Recursos finais ACPI de saída/IRQ identificados, numerações/domínios separados e limites de ownership, polaridade e estado registrados; nenhuma função elétrica presumida |
| 0.3 | Caminho TX/RX delimitado, erros propagados e **resposta válida do dispositivo no Linux**, com bytes e contexto preservados; critérios completos em [V03_SPI_TRANSPORT_GAPS.md](V03_SPI_TRANSPORT_GAPS.md) |
| 0.4 | Interrupções reais correlacionadas a recepção, validação e entrega de respostas MCU; contador de IRQ isolado é insuficiente |
| 0.5 | Consulta de identificação delimitada, significado do identificador estabelecido e resposta válida verificada no dispositivo; não confundir versão de CPU com Chip ID |
| 0.6 | Sequência mínima de inicialização documentada e validada, com pré-condições e falhas explícitas, sem alteração de firmware |
| 0.7 | Cadastro/verificação investigados e demonstrados com protocolo e tratamento de dados definidos, após os marcos físicos anteriores |
| 1.0 | Integração libfprint funcional e validada no Linux, após comprovar as capacidades necessárias do dispositivo |

A leitura diagnóstica de versão de CPU poderá servir como resposta de referência
para 0.3, se sua segurança e validade forem estabelecidas. Isso não conclui
automaticamente 0.5: o conteúdo e o objetivo da consulta de Chip ID são outros.

## Componentes de pesquisa e infraestrutura

Os nomes de referência passam a ser **núcleo de protocolo MCU offline**
(histórico `linux/v0.6`) e **processamento de eventos e concorrência offline**
(históricos `linux/v0.7` e `linux/v0.7-validation`). São rótulos documentais;
a cópia pública usa os caminhos da matriz de componentes e preserva os nomes KTAP.

Os 25 casos KUnit aprovados em Basic, lockdep, KASAN e KCSAN Strict têm
[evidência preservada](evidence/README.md) no escopo offline. Sua contribuição futura
está em [COMPONENT_TRACEABILITY.md](COMPONENT_TRACEABILITY.md). Eles não
representam inicialização segura 0.6 nem protocolo biométrico 0.7 concluídos.

## Prioridade e controle documental

A prioridade é concluir o transporte SPI funcional 0.3, resolvendo o bloqueio
`FF FF`. Não há desenvolvimento de 0.8 previsto nesta tarefa. Protocolo
biométrico e libfprint aguardam seus pré-requisitos.

Este roadmap e os quatro documentos relacionados orientam decisões atuais.
Relatórios antigos permanecem intactos como registros de sua época; menções
antigas a “v0.6/v0.7” devem ser interpretadas pela matriz de componentes. Estados
antigos de CI “pendente” não anulam os resultados posteriormente informados.

Esta entrega é exclusivamente documental. Não autoriza SPI, GPIO, IRQ física,
reset, PM, firmware, PSK, OTP, avaliação AML, bind/unbind ou carga de módulos.
`spi1.0`, identificado como flash, permanece excluído. O plano futuro está em
[NEXT_STEPS.md](NEXT_STEPS.md); nenhuma instrução ativa antiga deve ser executada
como consequência desta reorganização.
