# Manutenção local segura (MAINT-01)

O ponto de entrada continua sendo `scripts/maintenance/local-workspace.ps1`.
Requer PowerShell 7 e Python 3.11 ou posterior, somente biblioteca padrão.
Não participa do runtime nem da instalação distribuída ao usuário.

Informe explicitamente o workspace real quando usar uma worktree. Exemplo:

```powershell
$workspace = 'C:\Users\fabeg\Desktop\x360'
$report = "$workspace/.local/evidence/maintenance/maint-01"
pwsh -File scripts/maintenance/local-workspace.ps1 -Workspace $workspace -ReportDirectory $report -PythonExecutable python
# Revise summary.json, duplicates.json, binaries.json e plan.json.
pwsh -File scripts/maintenance/local-workspace.ps1 -Workspace $workspace -ReportDirectory $report -PythonExecutable python -Apply
```

O inventário grava apenas o diretório de relatório escolhido. Não migra nem
remove conteúdo. `-Clean` continua aceito como alias de inventário; `-Apply`
agora exige o relatório explícito de um plano conferido. A migração do layout
de outubro de 2026 é histórica: a ferramenta não repete suas regras antigas.

O SQLite guarda tamanho, mtime, identidade física, links, alocação, classe e
hash. Uma nova auditoria no mesmo relatório reutiliza hashes com metadados
iguais; a execução sempre confere conteúdo novamente. Hashing é sequencial,
agrupado inicialmente por tamanho, com orçamento `-HashGiB` (64 GiB padrão) e
prazo `-TimeoutSeconds` (1.200 s padrão). Arquivos relevantes acima do orçamento
ficam explicitamente adiados. Nunca interpretar a busca parcial como prova de
que o restante é único.

`-RefreshPlan` refina um inventário SQLite já concluído sem repetir a enumeração;
é uma etapa separada de `-Apply`. Confere a identidade dos candidatos e calcula
seus hashes novamente. Antes de excluir, a ferramenta redescobre worktrees e
referências atuais, confere os documentos do plano e adquire a trava de ambiente.

As únicas exclusões automáticas são objetos/intermediários declarados por
grafos CMake/Ninja, sob `.local/build` ou jobs privados históricos de distribuição,
com fontes presentes e sem dependências
em recibos ou relatórios. Árvores com recibos de preparação permanecem aquecidas,
inclusive recibos inválidos/desconhecidos. EXE, DLL, PDB, fontes AOT, stamps,
bibliotecas, caches de configuração, pacotes, originais e armazenamento persistente
ficam preservados. A próxima compilação de uma árvore limpa recompila seus objetos;
o tempo depende dessa árvore. A ferramenta não executa rebuild nem renova recibos.

Não há deduplicação destrutiva ou hardlinks novos. Snapshots, dumps, backups e
evidências permanecem pendentes mesmo quando hashes coincidem: igualdade não
prova independência dos caminhos nem ausência de referências.

A aposentadoria de dados de ensaios encerrados exige pedido explícito e caminhos
individuais: `-RetireTrial <caminhos> -InventoryReport <inventário existente>` gera
um plano separado. Só aceita os escopos de fixtures antigas identificados pela
política; não seleciona outros ensaios por idade. Confere catálogo, preparações,
armazenamento selecionado, worktrees e processos antes de executar com `-Apply`.
O MAINT-01 recebeu autorização adicional para esses dados, incluindo confirmação
específica do lote de 18 diretórios de 26/10. Logs, capturas e diretórios `run`
permanecem fora do plano. Diretórios vazios podem ser removidos com `--prune-trials`
após a execução; a remoção é individual e não recursiva.

Uma sessão atual independente só pode ser reconhecida por seu caminho exato e
SHA-256, com `-ReviewedProcessImage`. Outros caminhos/processos continuam bloqueando
a execução. O mesmo hash em outro caminho não cria uma exceção automática.
Árvores alteradas por builds paralelos desde o inventário são preservadas inteiras
pelo refinamento do plano. `concurrent-builds-preserved.json` registra essas árvores.

Links não são percorridos. A identidade de volume/arquivo impede contar o mesmo
hardlink duas vezes; arquivos com múltiplos links não são excluídos. Tamanho lógico
e alocação física são métricas distintas: Windows usa `FileStandardInfo.AllocationSize`
e, para sparse/compressed, `GetCompressedFileSizeW`. Isso exclui metadados do sistema
de arquivos. A variação do espaço livre do volume também inclui atividade externa.

`journal.jsonl` é sincronizado antes de cada exclusão. A ferramenta abre o arquivo
com acesso exclusivo, confere caminho real do handle, identidade, número de links,
tamanho, mtime e SHA-256; então marca a exclusão pelo mesmo handle. Um item recriado
após exclusão bloqueia a retomada desse plano. Uma interrupção entre excluir e
registrar conclusão pode ser retomada sem tocar outro arquivo no mesmo caminho.
Builds/runtimes ativos e arquivos abertos bloqueiam a operação.

O empacotamento privado evita a cópia adjacente de ferramentas usada apenas pelo
launcher de desenvolvimento. O ZIP continua com `x360-native.exe`, `app/x360n.exe`
e `app/x360n-extract.exe`, copiados de seus targets originais. Depois dos gates,
índices e ZIP concluídos, remove apenas intermediários do job privado e registra
proveniência/journal. `-KeepIntermediates` mantém esses objetos para diagnóstico.
Falhas/cancelamentos conservam tudo. Fontes, logs e produtos finais são mantidos.

Testes focados, sem jogos ou Computer Use:

```powershell
python tests/maintenance/test_workspace.py
pwsh -File tests/maintenance/package-intermediates.ps1
pwsh -File tests/maintenance/desktop-staging.ps1 -ToolWorkspace C:\Users\fabeg\Desktop\x360
```

Os testes usam fixtures descartáveis; o teste CMake verifica os dois modos de
staging com o compilador pinado, sem compilar/iniciar o launcher ou jogos.
