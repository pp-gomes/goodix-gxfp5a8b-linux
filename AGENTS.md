# Goodix GXFP5A8B

Consulte [ROADMAP.md](docs/ROADMAP.md), [CURRENT_STATUS.md](docs/CURRENT_STATUS.md), [COMPONENT_TRACEABILITY.md](docs/COMPONENT_TRACEABILITY.md), [V03_SPI_TRANSPORT_GAPS.md](docs/V03_SPI_TRANSPORT_GAPS.md) e [NEXT_STEPS.md](docs/NEXT_STEPS.md) antes de propor alterações. A prioridade é 0.3. Separe a correção da transação host (0.3-A) da resposta válida do sensor (0.3-B). Feche a etapa somente com os critérios A03-01–A03-07 satisfeitos. O roadmap segue até 1.0 sem etapa 0.8.

Trabalhe em `src/` sem backend físico. Use `tests/` e os componentes de pesquisa para regressão. Registre a diferença entre teste sintético, observação histórica e hipótese. Preserve os nomes, fontes e hashes históricos em [PROVENANCE.json](docs/PROVENANCE.json). Separe endereço SPI 0x00A8, opcode MCU A8 e consulta de Chip ID.

Por padrão, limite o trabalho a arquivos, inspeção estática e testes offline. Qualquer operação física exige plano revisado, identificação inequívoca, pré-condições elétricas e aprovação específica. Evite sudo, módulos, bind/unbind, SPI, GPIO, IRQ, reset, wakeup, PM, pinmux, AML, BIOS, firmware, PSK e OTP. Nunca acesse `spi1.0` ou `/dev/spi1.0`, a flash do notebook. Identifique o sensor pelos recursos ACPI, sem sondar barramentos ou depender de índices históricos.

Confira a revisão Ubuntu exata antes de transportar conclusões entre kernels. Preserve a trilha das evidências e os limites de cada perfil offline. Consulte [PUBLICATION.md](docs/PUBLICATION.md) antes de acrescentar materiais para publicação.

## Publicação automática controlada

Ao concluir uma tarefa de desenvolvimento, você está autorizado a criar um commit e executar `git push` automaticamente, somente na branch de desenvolvimento existente e correspondente à tarefa.

1. Confirme a branch atual e sua correspondência com a tarefa. Nunca publique automaticamente na `main`.
2. Execute os testes aplicáveis e verifique os resultados.
3. Execute `git diff --check` e verifique também o diff preparado para commit com `git diff --cached --check`.
4. Revise os arquivos modificados e novos da tarefa em busca de credenciais, dados privados, binários proprietários e logs sensíveis.
5. Adicione ao Git somente os arquivos relacionados à tarefa, indicando seus caminhos. Evite o uso indiscriminado de `git add .`.
6. Crie um commit descritivo seguindo Conventional Commits.
7. Execute o push para a branch de desenvolvimento existente e configure o upstream quando necessário.
8. Informe o hash do commit, a branch, os testes executados e o resultado do push.
9. Não execute commit ou push se algum teste necessário falhar, se houver arquivos suspeitos ou se a alteração incluir material de terceiros cuja publicação não foi autorizada.
10. Nunca execute `push --force`, rebase destrutivo, `reset --hard`, exclusão de branches ou publicação automática de Releases.

A `main` só pode ser atualizada após a aprovação de um Pull Request pelo usuário. Apresente esta alteração de política para revisão antes de criar o primeiro commit de configuração.

A autorização permanente se limita ao fluxo Git. Alterações de BIOS ou firmware, acesso físico a SPI, GPIO ou IRQ, reset, gerenciamento de energia e carregamento de módulos continuam exigindo autorização específica e as pré-condições acima. A flash do notebook, `spi1.0` (`/dev/spi1.0`), permanece completamente excluída. Valide o fluxo de publicação sem operações físicas de hardware.

## Relatório obrigatório de execução e transferência de contexto

Ao encerrar cada tarefa, gere automaticamente um relatório técnico em Markdown
para o proprietário encaminhar ao ChatGPT. A regra vale para entregas
concluídas ou parciais, investigações inconclusivas, falhas de compilação ou de
testes e impedimentos de publicação. Se a execução permitir, registre o que
foi feito mesmo diante de um bloqueio ou interrupção.

- No início, registre a branch, o HEAD local, o HEAD remoto quando acessível,
  o estado da árvore de trabalho e as alterações preexistentes relevantes.
- Use as 12 seções do [modelo obrigatório](docs/templates/EXECUTION_REPORT.md).
  Escreva em português do Brasil. Identifique fatos, inferências e lacunas,
  preserve valores e referências técnicas relevantes e dê contexto suficiente
  para planejar a próxima tarefa a partir de um único arquivo. Use trechos
  mínimos de código ou erro quando necessários. Exclua credenciais, dados
  biométricos, PSK, OTP, informações pessoais, código proprietário e logs brutos.
- Valide as alterações e siga a política de publicação acima para toda
  alteração útil, validada e publicável. A autorização Git já concedida vale
  para as tarefas futuras. Solicite somente aprovações técnicas exigidas pelo
  ambiente ou sandbox. Se o push falhar, preserve os commits locais e
  registre a causa exata. Confirme a publicação antes de declará-la concluída.
- Depois das validações e da tentativa de publicação, crie, se necessário,
  `~/Projetos/goodix-relatorios/` e seu subdiretório `historico/`.
  Grave o relatório em `historico/AAAA-MM-DD_HHMMSS_descricao-curta.md`,
  com o horário local e criação exclusiva. Em caso de colisão, use um
  novo horário. Mantenha os relatórios completos fora da árvore Git e
  excluídos de publicação automática.
- Atualize `~/Projetos/goodix-relatorios/LATEST.md` com o conteúdo integral
  do relatório histórico. Verifique a existência dos dois arquivos, a
  igualdade do conteúdo e a presença do título e das 12 seções. Se uma
  restrição de escrita impedir a entrega, registre a limitação e informe
  quais arquivos puderam ser gerados.
- Na resposta final, informe o caminho absoluto do relatório e de
  `LATEST.md`, o resultado da tarefa e os commits publicados. Registre a
  próxima decisão técnica no relatório. Aguarde nova solicitação para
  iniciar trabalho fora do escopo da tarefa.
