# Reorganização do repositório — 17/10/2026

## Auditoria e proposta (antes das mudanças)

Base: `721997147f89cdbd1f92cf5c5d45cd68d8961aed`, 680 entradas
rastreadas. O SDK já contém modificações locais esperadas; há também
`docs/research/recomp-handoff.md` não rastreado. Esses trabalhos não pertencem
aos commits da reorganização. A captura inicial fica em `.local/reorganization/`.

Problemas observados: scripts de setup, build, análise, captura, benchmarks e
empacotamento no mesmo diretório; testes do desktop em `src/`; recursos Win32
misturados ao modelo; módulos CMake sem agrupamento; documentação operacional e
relatórios históricos sem índice. `.local` mistura configurações persistentes,
originais, toolchains, builds, evidências e dezenas de scripts/fontes avulsos.

Estrutura proposta:

```text
src/
  supervisor/              contratos e CLI existentes
  content/                 ferramentas de conteúdo existentes
  desktop/{model,win32,resources}/
  native/{commands,d3d12,memory,pipeline,textures,shaders}/
tests/{desktop,supervisor,content,native,sdkcheck,...}/
scripts/
  bootstrap.ps1, devshell.cmd, build-title.ps1, prepare-tools.ps1
  setup/, build/, analysis/, diagnostics/, performance/, packaging/, validation/
cmake/{sdk,build,runtime,distribution}/
docs/
  README.md                índice por assunto
  guides/, reports/        operação e evidências históricas
titles/<identidade>/       receitas e originais isolados por versão
.local/
  inputs/, user/, userdata/, catalog/, keys/   dados preservados
  toolchains/, build/, aot/, preparation/      caminhos de identidade estáveis
  cache/, evidence/, archive/, tmp/           saídas classificadas
```

Etapas: (1) registrar auditoria e proposta; (2) mover ferramentas e CMake,
corrigindo consumidores; (3) separar código/testes/recursos e documentação;
(4) migrar saídas locais com inventário, excluir apenas resíduos regeneráveis
comprovados; (5) compilar, executar regressões focadas e registrar resultados.
Todas as movimentações rastreadas usam `git mv`. Nenhum push ou Release.

## Limites e preservação

Não alterar versões fixadas, patches, hashes de originais, semântica PPC/XAM,
backends, opções de compilação ou contratos funcionais. Não mover inputs,
perfis/saves, catálogos, SDK, toolchains, worktrees ou builds ativos: caminhos
absolutos, recibos e caches CMake podem depender deles. Arquivos gerados de AOT
continuam ignorados nos caminhos esperados pelo SDK; os seis `rexglue.cmake`
rastreados são configurações diferentes, não duplicatas descartáveis.

Evidências existentes são preservadas byte a byte. Referências internas de logs
antigos continuam registrando o caminho original; o mapa de migração permite
localizá-los. Recibos de preparação não são reemitidos nem editados para aparentar
validade após mudança de fontes. Backups e repositórios de finalidade incerta são
preservados. Exclusões locais exigem alvo absoluto dentro do workspace,
comprovação e registro antes da remoção. Não executar scripts avulsos encontrados
em `.local`.

Não lançar jogos ou usar Computer Use. A alteração de caminhos justifica
regressões de build, preparação, isolamento, biblioteca, persistência e PPC;
não justifica repetir sessões visuais nem executar a suíte completa do SDK.

## Resultados

Preenchidos após a implementação e validação.
