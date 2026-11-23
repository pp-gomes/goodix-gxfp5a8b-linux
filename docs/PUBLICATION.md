# Revisão de publicação

A cópia foi montada por seleção de arquivos. [PROVENANCE.json](PROVENANCE.json) relaciona cada arquivo copiado ou adaptado à origem, com SHA-256 nos dois lados. Origens com `!` identificam membros de ZIP. A tabela do [histórico](../historical/README.md) registra a função dos snapshots.

## Exclusões

| Origem | Motivo |
|---|---|
| `driver/`, `goodix-driver/` e pacotes do fabricante | Binários proprietários e arquivos de distribuição |
| `analysis/`, bancos Ghidra, bundles REA e pseudocódigo/assembly extraído | Direitos de redistribuição e dados de análise a revisar |
| `dsdt.dsl`, dumps ACPI/BIOS/memória e firmware | Dados de plataforma e artefatos de terceiros |
| ZIPs, árvores duplicadas de entrega, produtos de compilação e dependências empacotadas | Tamanho, duplicação ou risco de republicar material alheio |
| Logs de build, diagnóstico e runtime completos, diretórios `validation/` e `ci/goodix/local-validation/` | Caminhos e dados de ambiente. Os resultados KUnit usam membros selecionados dos artefatos |
| Configurações privadas, caches, repositórios internos e arquivos fora do projeto | Escopo e privacidade |
| Relatórios extensos de Windows, GPIO, plataforma, PM e comparações Ubuntu | Dependência de acervo privado, detalhes de terceiros e necessidade de revisão individual |
| READMEs offline antigos, `VALIDATION-STATUS.md` e `tools/package-source.py` da v0.7-validation | Caminhos e afirmações de época, dados de ambiente ou empacotamento da estrutura anterior. O material atual está nos guias de reprodução |

Os links para relatórios privados nos documentos de planejamento apontam para esta seção. Os nomes de ZIPs permanecem como referências de origem.

## Pendências antes de publicar

- Confirmar a licença aplicável ao projeto como um todo. Há cabeçalhos SPDX GPL-2.0-only no núcleo C. A cobertura dos scripts, documentos e snapshots deve ser confirmada pelo autor.
- Confirmar a autorização para divulgar os pequenos vetores de protocolo observados no Windows em `research/offline-mcu/fixtures/windows-a8.json` e `tests/fixtures.h` das árvores KUnit. Eles contêm bytes diagnósticos e identificação de versão, que os testes usam como referência.
- Revisar os READMEs históricos extraídos dos ZIPs, que documentam comandos físicos da época.
- Conferir o acesso público aos links do Goodix-KCSAN. Nesta sessão o endpoint da execução 41592466405 não pôde ser aberto. A evidência local pôde ser verificada.

## Verificação da cópia

- 23 testes de host e 32 testes dos helpers de CI aprovados.
- Núcleo C com 61.084 verificações aprovadas por execução, incluindo a variante ASan/UBSan. Auditoria de dependências do objeto aprovada.
- Paridade com UBSan aprovada, com 5.476 verificações e 5.398 pacotes diferenciais.
- Quatro KTAPs históricos verificados contra o fonte da suíte e os hashes dos 12 arquivos integrados. Esta preparação não repetiu o boot UML/QEMU.
- A matriz UML da cópia contém Basic, lockdep e KASAN. O KCSAN Strict usa o workflow QEMU, pois o perfil KCSAN histórico falhou por incompatibilidade em UML.
