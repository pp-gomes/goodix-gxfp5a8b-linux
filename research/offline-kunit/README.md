# Validação KUnit offline

Origem histórica `linux/v0.7-validation`. O [fonte da suíte](gx_kunit.c) declara 25 casos. A [paridade](tools/check-protocol-parity.py) compara o núcleo C de usuário com a edição kernel. O [backend](gx_transport_disabled.c) retorna `GX_HARDWARE_DISABLED`.

Use os guias [UML](../../ci/goodix/README.md) e [QEMU KCSAN Strict](../../ci/goodix/KCSAN-QEMU.md) para reproduzir. Os [artefatos históricos](../../docs/evidence/README.md) documentam as quatro configurações aprovadas.
