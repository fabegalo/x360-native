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
backends, opções de compilação ou contratos funcionais. A proposta inicial
preservava os caminhos de inputs; o mantenedor ampliou explicitamente o escopo
para migrá-los, prever discos/DLCs e retirar defaults de dashboard do código
genérico. A migração atualiza manifestos/catálogo e confere os originais.
Perfis/saves, SDK, toolchains, worktrees e builds ativos mantêm suas raízes:
caminhos absolutos, recibos e caches CMake podem depender deles. Arquivos gerados de AOT
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

## Estrutura implementada

- 52 movimentações de ferramentas/CMake e 84 de código/testes/documentos por
  `git mv`; seis grupos CMake de testes mantêm os diretórios de saída e nomes.
- Desktop separado em modelo, Win32 e recursos; testes saíram de `src/`.
  Native foi agrupado fisicamente por responsabilidade, sem desenvolvimento
  de novas funcionalidades ou mudança de backend.
- [Índice](README.md), [filetree](filetree.txt) e
  [layout de inputs](guides/content-layout.md). Scripts, CMake e testes têm READMEs.
- `cmake/x360n_sdk_backports.cmake` e `scripts/ninja-guard.ps1` são encaminhamentos
  deliberados para projetos/caches locais já existentes.
- `.local/evidence/` agrupa logs, execuções, benchmarks, auditorias e validações.
  As quatro junctions internas de uma validação histórica foram retargetadas
  para os mesmos dados no novo endereço, sem copiar ou misturar originais.
- `.local/archive/` preserva fontes temporárias, experimentos e backups cuja
  finalidade não permite exclusão. Somente configurações/estado permanecem soltos.
- `inputs` contém `dashboards/` e `games/`, sem caches gráficos ou fixtures
  duplicados. A migração conferiu 626 XEX/fontes; a cópia Unicode de Iruka foi
  excluída somente após comparar os dez arquivos ao conjunto preservado.
- Importações novas usam Title ID, disco e fingerprint completo; reimportação
  conserva o `content_root` de catálogos antigos. DLCs permanecem fora do root
  do executável base; não se afirma novo suporte a instalação ou troca de discos.
- O guard de dashboard usa manifesto/preparação; o supervisor aplica opções
  diagnósticas declaradas pelo perfil. Não há caminho de uma versão fixa no
  supervisor/desktop. Os adaptadores de cada versão continuam específicos.

Os outputs AOT genéricos já ficam em `.local/build/aot/<identidade>/generated`.
Os títulos manuais conservam `titles/<título>/generated/`, contrato do template
SDK pinado. Somente `rexglue.cmake`, sem código derivado de XEX, é rastreado;
não mover ou apagar esses outputs por aparência. Caches/intermediários comprovados
podem ser removidos, mas recibos antigos não tornam um rebuild dispensável.

## Validação e evidências

Capturas de estado, comandos limitados, logs e JUnit em `.local/reorganization/`.
O mapa e os hashes de inputs ficam em `inputs-migration/`; cada aplicação da
manutenção escreve `local-<timestamp>/journal.jsonl` antes de mover/excluir.
Comparação do diff inicial/final do SDK, hashes dos patches/lock e preservação
do arquivo não rastreado constam em `structure-audit.json`.

Resultados finais de build/regressões e totais da limpeza são registrados abaixo
quando as execuções terminarem. Não executar jogos para essa reorganização.
