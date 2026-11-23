# Testes de host

Execute `python3 -B -m unittest discover -s tests -v` na raiz. O diagnóstico usa um transporte em memória. Os testes do coletor de metadados usam diretórios temporários artificiais e exercitam apenas a interface de fixture do script.

Execute `make -C tests/spi test sanitize` na raiz para a integração C
simulada do [transporte SPI 0.3](../src/spi/README.md).
