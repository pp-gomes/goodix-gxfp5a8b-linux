# Snapshots históricos

| Snapshot | Origem | Função investigada |
|---|---|---|
| [passivo](passivo/) | `linux/Goodix_GXFP5A8B_driver_passivo.zip` | Associação ACPI/SPI |
| [v0.2](v0.2/) | `linux/gxfp5a8b_acpi_probe_v02.zip` | Inventário ACPI |
| [v0.3](v0.3/) | `linux/Goodix_GXFP5A8B_probe_v03.zip` | Contador opcional de IRQ |
| [v0.4](v0.4/) | `linux/GXFP5A8B_v04_corrigido.zip` | Leitura experimental da versão de CPU |
| [v0.5](v0.5/) | `linux/GXFP5A8B_v05_E5_cpu_diagnostico.zip` | Diagnóstico experimental com E5 |
| [linux/v0.7](linux/v0.7/) | `linux/v0.7/` | Primeira árvore KUnit offline |

O núcleo histórico `linux/v0.6` está em [offline-mcu](../research/offline-mcu/README.md). A variante `linux/v0.7-validation` está em [offline-kunit](../research/offline-kunit/README.md). O [manifesto](../docs/PROVENANCE.json) registra arquivos, membros de ZIP, SHA-256 e alterações da cópia pública.

Os READMEs extraídos dos cinco ZIPs preservam instruções de época para rastreabilidade. Trate comandos de carga, remoção e diagnóstico físico como documentação histórica. Os snapshots ficam fora dos workflows. Alguns atributos sysfs disparam SPI durante a leitura.
