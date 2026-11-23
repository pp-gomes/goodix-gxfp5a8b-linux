# Arquitetura e decisões

**Transporte 0.3.** A sessão em [src/](../src/README.md) serializa a seleção de endereço e a leitura como um único diagnóstico lógico. O contrato registra status, comprimento efetivo e RX por mensagem. O backend em memória exercita respostas, falhas, cancelamento e remoção.

**Protocolo e concorrência.** O [núcleo MCU](../research/offline-mcu/README.md) implementa formatação, checksums e parsing em C. A [suíte KUnit](../research/offline-kunit/README.md) exercita o núcleo adaptado para tipos do kernel, staging e troca de mensagens com eventos sintéticos. A fronteira de transporte do KUnit retorna `GX_HARDWARE_DISABLED`.

**Identidade e pré-condições.** A instância precisa vir do recurso ACPI do cliente Goodix. Um futuro adaptador dependerá de revisão de CS, status, comprimento, timeout e buffers nos caminhos SPI core e pxa2xx, incluindo dependências ACPI, PCI/LPSS e runtime PM. A comparação das revisões Ubuntu `7.0.0-42.42~24.04.1` e `7.0.0-43.43~24.04.1` exige fontes exatos e autenticados. A matriz de [lacunas SPI](V03_SPI_TRANSPORT_GAPS.md) registra as hipóteses e seus limites.

**Histórico e evidências.** Os experimentos anteriores ficam em [historical/](../historical/README.md). Os [resultados KUnit](evidence/README.md) mantêm a proveniência e o escopo offline.
