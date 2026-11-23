# Evidência KUnit histórica

Cada pasta contém o KTAP extraído da suíte, status, resultado verificado e hashes dos fontes integrados. Cada membro foi conferido contra o manifesto do ZIP local de origem. Os quatro KTAPs foram novamente validados com `ci/goodix/verify_ktap.py` e o fonte KUnit preservado.

| Perfil | Evidência | Execução | Resultado |
|---|---|---|---|
| Basic | [KTAP](basic/gx-offline-v07.ktap) | 41587557124 | 25/25 |
| lockdep | [KTAP](lockdep/gx-offline-v07.ktap) | 41587557124 | 25/25 |
| KASAN | [KTAP](kasan/gx-offline-v07.ktap) | 41587557124 | 25/25 |
| KCSAN Strict | [KTAP](kcsan-strict/gx-offline-v07.ktap) e [instrumentação em runtime](kcsan-strict/runtime-instrumentation.json) | 41592466405 | 25/25 |

O KCSAN Strict aprovado usou QEMU x86_64. O job UML de outra execução não fornece esse resultado. O [manifesto](../PROVENANCE.json) identifica os ZIPs por SHA-256. Os logs brutos e os ZIPs permanecem no acervo original. [Execução no Goodix-KCSAN](https://github.com/pp-gomes/Goodix-KCSAN/actions/runs/41592466405)
