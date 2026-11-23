# Goodix GXFP5A8B

Consulte [ROADMAP.md](docs/ROADMAP.md), [CURRENT_STATUS.md](docs/CURRENT_STATUS.md), [COMPONENT_TRACEABILITY.md](docs/COMPONENT_TRACEABILITY.md), [V03_SPI_TRANSPORT_GAPS.md](docs/V03_SPI_TRANSPORT_GAPS.md) e [NEXT_STEPS.md](docs/NEXT_STEPS.md) antes de propor alterações. A prioridade é 0.3. Separe a correção da transação host (0.3-A) da resposta válida do sensor (0.3-B). Feche a etapa somente com os critérios A03-01–A03-07 satisfeitos. O roadmap segue até 1.0 sem etapa 0.8.

Trabalhe em `src/` sem backend físico. Use `tests/` e os componentes de pesquisa para regressão. Registre a diferença entre teste sintético, observação histórica e hipótese. Preserve os nomes, fontes e hashes históricos em [PROVENANCE.json](docs/PROVENANCE.json). Separe endereço SPI 0x00A8, opcode MCU A8 e consulta de Chip ID.

Por padrão, limite o trabalho a arquivos, inspeção estática e testes offline. Qualquer operação física exige plano revisado, identificação inequívoca, pré-condições elétricas e aprovação específica. Evite sudo, módulos, bind/unbind, SPI, GPIO, IRQ, reset, wakeup, PM, pinmux, AML, BIOS, firmware, PSK e OTP. Nunca acesse `spi1.0` ou `/dev/spi1.0`, a flash do notebook. Identifique o sensor pelos recursos ACPI, sem sondar barramentos ou depender de índices históricos.

Confira a revisão Ubuntu exata antes de transportar conclusões entre kernels. Preserve a trilha das evidências e os limites de cada perfil offline. Consulte [PUBLICATION.md](docs/PUBLICATION.md) antes de acrescentar materiais para publicação.
