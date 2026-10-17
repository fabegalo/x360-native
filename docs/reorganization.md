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
  setup/, build/, analysis/, diagnostics/, performance/, packaging/, validation/, maintenance/
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
- `cmake/x360n_sdk_backports.cmake`, `scripts/ninja-guard.ps1` e
  `scripts/apply-sdk-patches.ps1` são encaminhamentos deliberados para projetos,
  caches locais e o comando registrado no lock já existente.
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

Capturas de estado, comandos limitados, logs e JUnit em
`.local/evidence/reorganization/2026-10-17/`.
O mapa e os hashes de inputs ficam em `inputs-migration/`; cada aplicação da
manutenção escreve `.local/evidence/maintenance/<timestamp>/journal.jsonl`
antes de mover/excluir. As primeiras aplicações ficaram em `local-<timestamp>/`
dentro do pacote de evidências da reorganização.
Comparação do diff inicial/final do SDK, hashes dos patches/lock e preservação
do arquivo não rastreado constam em `structure-audit.json`.

Os comandos nos logs registram os caminhos da execução; o pacote de evidências
foi centralizado ao final. Helpers temporários nesse pacote são fontes de
auditoria, não pontos de entrada operacionais. A manutenção permanente está em
`scripts/maintenance/`.

### Compilação e regressões de 17/10/2026

| Verificação | Resultado |
|---|---|
| Raiz, Debug, preset `win-amd64` | Build aprovado, 87,297 s |
| Desktop-only, configuração Ninja independente | Build e `desktop.catalog` aprovados, 271,047 s no fluxo completo |
| Preparação normal da Retail 17559 | Aprovada, recibo novo, 719,578 s |
| Preparação normal de Motocross Madness | Aprovada, recibo novo, 805,359 s |
| Preparação normal de Castle Crashers | Aprovada, recibo novo, 329,297 s |
| Preparação normal de Forza Horizon | Aprovada, recibo novo, 761,594 s |
| Preparação normal de Iruka | Aprovada, recibo novo, 341,078 s |
| Regressões selecionadas da raiz | 129 casos distintos aprovados após retestes focados; zero pendentes/skips no resultado final |
| Automação de distribuição, testes Python com mocks | 17 aprovados; nenhuma publicação real |
| Registro CTest | 1.891 → 1.892 casos, nenhum removido; um caso novo de argumentos por perfil |
| Guard da Retail 6717 | Rejeição esperada de `XenonJKLatin.xtt` sem hash identificado; sem fallback |

Os tempos descrevem os comandos registrados, com cache/concorrência diferentes;
não são benchmark nem demonstração de ganho de performance. O resumo por caso
está em `regression-summary.json`, com o JUnit original e os retestes preservados.
O teste da licença real usa metadados do Castle instalado e stores temporários;
não executa o jogo. O inventário foi repetido após deduplicar os inputs.

A primeira rodada teve três falhas e um skip: fixture do desktop ainda usava
o caminho antigo; o teste de assets comparava entradas opcionais como obrigatórias;
os recibos de avatares ainda correspondiam às fontes anteriores; a imagem Arcade
não estava informada no ambiente. Os ajustes preservaram a igualdade estrita das
dependências obrigatórias, verificaram as opcionais e regeneraram os recibos pelo
fluxo normal. O primeiro reteste também expôs uma comparação de ponteiros de
strings no teste; corrigida para comparação de valores. Cada caso afetado passou
no reteste focado. Uma quebra de linha no diagnóstico esperado da 6717 exigiu
normalizar espaços no helper de auditoria; a rejeição do CMake já estava correta.

O codegen normaliza TOML e `rexglue.cmake` para LF. A primeira preparação recusou
corretamente a mudança de bytes entre captura e publicação. Esses arquivos agora
têm LF explícito em `.gitattributes`; a preparação subsequente gerou recibos
válidos, sem reduzir os guards. A execução inicial sob sandbox teve uma falha de
criação de processo do compilador; a compilação no ambiente nativo passou.

Não foram executados jogos, sessões visuais, benchmarks ou a suíte completa do
SDK. Estes resultados não ampliam as certificações gráficas, de gameplay ou de
save/load comercial registradas em STATUS.

### Limpeza e preservação finais

Foram removidos **8.919 arquivos**, somando
**6.552.722.112 bytes (6.10 GiB)**
de tamanho lógico. Isso não é medição de espaço livre líquido: os builds também
geraram artefatos durante o trabalho. A manutenção confirmou 95
movimentos locais, além da migração dos inputs e dos 136 `git mv` rastreados.

A limpeza inclui downloads já instalados/verificados, bytecode Python,
intermediários CMake/objetos/PCH sem referência em recibos e caches de builds
históricos. A cópia de Charlie Murder tinha 983 arquivos/383.469.608 bytes,
todos comparados por SHA-256 à cópia preservada; o catálogo mantém a identidade
e aponta para essa cópia. A fixture Unicode de Iruka tinha dez arquivos idênticos.
Diretórios temporários vazios foram removidos, sem criar discos/DLCs vazios.

As duas junctions instaladas de Castle Crashers agora apontam para o mesmo
conteúdo em `inputs/games/castle_crashers/content`; o XEX manteve seu SHA-256.
Nenhum perfil, save, cabeçalho de licença ou dado persistente foi regravado.
Os 626 registros originais de XEX/fontes conferem com os hashes iniciais; aliases
e cópias idênticas convergem para 595 destinos preservados.

Após a limpeza, todos os arquivos certificados pelos cinco recibos novos
mantêm seus hashes. Schema, roles, inventário de EXE/DLLs e globs também foram
conferidos. Recibos, produtores, código AOT e artefatos finais foram preservados;
os próximos builds recompilam os intermediários descartados.

O diff prévio do SDK foi preservado integralmente. O lock e todos os `.patch`
mantêm os bytes originais; versões e hashes fixados não mudaram. Quatro arquivos
textuais protegidos diferem somente em LF/CRLF, sem alteração dos valores de
identidade. A pesquisa local não rastreada permaneceu fora dos commits.

Fontes exportadas, experimentos, bundles e backups de finalidade incerta foram
arquivados com inventário, sem descarte por aparência. Worktrees registradas,
toolchains, SDK e roots de usuário foram preservados. Exemplos históricos de
`.local/tmp` e distribuição continuam nos logs; o mapa abaixo localiza o acervo.

| Endereço anterior | Acervo atual |
|---|---|
| `.local/c8` | `.local/evidence/c8` |
| `.local/integration-avatars` | `.local/evidence/integration-avatars` |
| `.local/locale-replay81` | `.local/archive/experiments/locale-replay81` |
| `.local/patchwork` | `.local/archive/experiments/patchwork` |
| `.local/projects` | `.local/archive/projects` |
| `.local/tmp` | `.local/archive/work/tmp-20261017-233757-5492296` |
| `.local/test-temp` | `.local/archive/work/test-temp-20261017-233757-5492296` |
| `.local/distribution` | `.local/archive/work/distribution-20261017-233757-5492296` |
| `.local/collaboration-python` | `.local/archive/work/collaboration-python-20261017-233757-5492296` |

O pacote `.local/evidence/reorganization/2026-10-17/` contém os resultados, resumos por caso, hashes,
inventários, mapas, comandos e falhas iniciais. A manutenção futura usa
`scripts/maintenance/local-workspace.ps1` (plano por padrão; aplicação explícita).
O parser verificou os scripts PowerShell; a auditoria não encontrou links de
documentação ou includes relativos quebrados. Nenhum push, tag ou Release.
