# Núcleo MCU offline

Origem histórica `linux/v0.6`. As funções em [src/](src/) constroem e analisam pacotes em memória. Os [testes C](tests/test_protocol.c) usam [vetores diagnósticos](fixtures/windows-a8.json) provenientes de observações Windows relatadas, com o escopo indicado no JSON.

Execute `make -C research/offline-mcu test sanitize audit` na raiz. São necessários compilador C, Python 3, Make e binutils.
