# Goodix GXFP5A8B no Linux

Projeto de engenharia reversa e desenvolvimento de um driver Linux para o leitor de impressões digitais Goodix GXFP5A8B do **LG Gram 14Z90N-V.BR51P1**. O objetivo é habilitar cadastro e verificação de digitais via libfprint. O alvo atual informado é Linux Mint 22.3, base Ubuntu Noble, x86_64, kernel `7.0.0-43-generic`. Os registros anteriores usam `7.0.0-42-generic`.

**O transporte SPI funcional 0.3 está em desenvolvimento.** A leitura diagnóstica retornou `FF FF` nas tentativas históricas no Linux e `00 A1` na referência Windows. A causa permanece aberta. O sensor ainda não demonstrou comunicação válida no Linux.

A investigação combina C e interfaces do Linux Kernel para SPI, ACPI e GPIO, análise estática do driver Windows com REA e Ghidra e validação offline com Python, KUnit, UML, QEMU e GitHub Actions.

## Roadmap funcional

| Versão | Capacidade | Estado |
|---|---|---|
| 0.1 | Associação ACPI/SPI passiva | Reconhecimento histórico, fechamento documental parcial |
| 0.2 | Inspeção GPIO/IRQ | Recursos parcialmente caracterizados |
| 0.3 | Transporte SPI | Em desenvolvimento, bloqueado pela ausência de resposta válida |
| 0.4 | Recepção de interrupções | Pendente |
| 0.5 | Consulta de Chip ID | Pendente |
| 0.6 | Inicialização segura | Pendente |
| 0.7 | Protocolo biométrico | Pendente |
| 1.0 | Integração com libfprint | Pendente |

Os nomes históricos `v0.6` e `v0.7` identificam componentes offline. O [roadmap oficial](docs/ROADMAP.md) define os critérios dos marcos físicos.

## Arquitetura

| Caminho | Conteúdo |
|---|---|
| [src/](src/README.md) | Contrato de transporte 0.3, sessão de diagnóstico e backend em memória |
| [research/offline-mcu/](research/offline-mcu/README.md) | Núcleo C de protocolo e regressão em espaço de usuário |
| [research/offline-kunit/](research/offline-kunit/README.md) | Núcleo kernel, staging, concorrência e eventos sintéticos |
| [historical/](historical/README.md) | Snapshots dos experimentos e correspondência com os nomes originais |
| [tests/](tests/README.md) | Diagnóstico simulado e testes com árvores artificiais de metadados |
| [ci/](ci/goodix/README.md) e [.github/workflows/](.github/workflows/) | Automação de testes offline |
| [docs/](docs/ARCHITECTURE.md) | Arquitetura, decisões, investigação SPI e evidências |

## Resultados KUnit

Os mesmos **25 casos passaram em cada perfil**, com zero falhas e zero casos pulados. Os resultados abaixo foram conferidos nos artefatos históricos, contra a lista de casos do fonte preservado.

| Perfil | Resultado | Ambiente | Execução |
|---|---|---|---|
| Basic | 25/25 | UML | 41587557124 |
| lockdep | 25/25 | UML | 41587557124 |
| KASAN | 25/25 | UML | 41587557124 |
| KCSAN Strict | 25/25 | QEMU x86_64 | 41592466405 |

As execuções exercitam código e eventos offline. A instrumentação KCSAN Strict foi confirmada em runtime com duas CPUs virtuais online. Consulte as [evidências preservadas](docs/evidence/README.md), o [repositório Goodix-KCSAN](https://github.com/pp-gomes/Goodix-KCSAN) e a [execução KCSAN Strict](https://github.com/pp-gomes/Goodix-KCSAN/actions/runs/41592466405).

## Reprodução offline

Em Linux com Python 3, compilador C, Make e binutils, execute na raiz do projeto.

```sh
python3 -B -m unittest discover -s tests -v
python3 -B -m unittest discover -s ci/goodix/tests -v
make -C tests/spi test sanitize
make -C research/offline-mcu test sanitize audit
python3 -B research/offline-kunit/tools/check-protocol-parity.py
```

Para KUnit com Linux v6.12, os workflows manuais [UML](.github/workflows/goodix-kunit.yml) e [QEMU](.github/workflows/goodix-kcsan-qemu.yml) usam ambientes isolados. Os guias de [UML](ci/goodix/README.md) e [KCSAN Strict](ci/goodix/KCSAN-QEMU.md) descrevem dependências, configuração e verificação dos resultados. A reprodução não requer o notebook alvo.

## Próximos passos

A etapa 0.3-A já conta com o [diagnóstico C offline](src/spi/README.md) e a [comparação dos fontes Ubuntu exatos](src/README.md#comparação-dos-fontes-ubuntu--42-e--43). Ainda precisa confirmar a associação ACPI no ambiente previsto e o estado elétrico, incluindo CS e temporização. A etapa 0.3-B depende de uma resposta Linux interpretável, verificável e repetível. Depois disso, o roadmap segue por IRQ, Chip ID, inicialização, protocolo biométrico e integração com libfprint. Veja os [critérios A03-01–A03-07](docs/V03_SPI_TRANSPORT_GAPS.md) e o [plano de investigação](docs/NEXT_STEPS.md).

A reprodução se limita aos testes offline. Os módulos em `historical/` são material de estudo. **`spi1.0` é a flash do notebook e deve permanecer intocada.** A [revisão de publicação](docs/PUBLICATION.md) registra os itens excluídos e pendentes.
