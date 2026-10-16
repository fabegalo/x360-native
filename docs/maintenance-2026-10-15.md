# Auditoria documental e limpeza local — 2026-10-15

Base: `1844245a36f2faa0ee7172581b1c7ababb89c202`. Escopo: documentação,
artefatos regeneráveis e higiene Git; sem alteração funcional, reescrita de
histórico, publicação de Release, sessão de jogo ou Computer Use.

## Documentação

Inventariados 9.810 arquivos Markdown: 46 mantidos pelo projeto, 259 do SDK e
dependências fixadas e 9.505 em cópias locais de distribuição, ferramentas,
referências e evidências. Os 46 documentos próprios passaram pela varredura de
estrutura, links locais e afirmações de estado. Documentação upstream e cópias
de versões anteriores não foram reescritas: pertencem à revisão que descrevem.

- `STATUS.md` e a matriz de compatibilidade foram consolidados, substituindo
  resumos duplicados e contraditórios por estado atual e links para as evidências.
- README, checklist, contexto dos agentes, arquitetura e guias agora distinguem
  C7/C8, Forza pela My Games, Native experimental/suspenso e Xenos X2. Nenhum
  resultado histórico foi promovido a novo aceite de gameplay ou performance.
- Registros iniciais de dashboard, Castle, Motocross e preparação ganharam
  referências às validações posteriores. Relatórios de medições, falhas,
  licenças, integração e notas de releases continuam preservados.
- Removido `LICENSE_PROPOSAL.md`: proposta encerrada, com rascunho duplicado da
  licença já aprovada. `LICENSE`, créditos e termos de terceiros permanecem.
- Download aponta para a preview.20 existente. Configurações reais do GitHub
  foram consultadas: proteção por rulesets, PR e check obrigatório; relato
  privado de vulnerabilidades ainda desabilitado. Nenhuma configuração mudou.
- `.gitignore` passou a cobrir intermediários CMake/compilador, caches Python,
  diretórios descartáveis de testes e extensões de originais fora de `.local`.
  Patches, fontes e `generated/rexglue.cmake` continuam versionáveis.

O link `app/workspace/docs/ICON_PROVENANCE.md` nos avisos de terceiros é
intencionalmente relativo ao ZIP distribuído; o próprio texto informa o caminho
equivalente no checkout. Os demais destinos locais de links Markdown existem.

## Espaço e causa do crescimento

Medição por soma dos tamanhos dos arquivos regulares, incluindo arquivos
ignorados e Git, **sem seguir junções/symlinks**. GB usa 10⁹ bytes; GiB usa 2³⁰.
Não é uma medição de clusters NTFS nem do tamanho remoto do GitHub. Worktrees
fora deste checkout não entram na conta e não foram removidas.

| Grupo antes da limpeza | GB | Decisão |
|---|---:|---|
| `.local/benchmarks` | 42,08 | Remover somente quatro cópias de conteúdo conferidas; preservar seed, resultados, logs, perfis e caches das medições |
| `.local/inputs` | 41,82 | Preservar integralmente jogos, ISOs, dashboards, recursos e metadados |
| `.local/validation` | 35,36 | Remover somente uma cópia de conteúdo conferida; preservar evidências e instalações |
| `.local/build` | 22,80 | Remover intermediários regeneráveis, mantendo executáveis, DLLs, código gerado e logs |
| `.local/distribution` | 10,52 | Remover intermediários de build; preservar snapshots de fontes e artefatos históricos |
| `.local/user` | 7,79 | Preservar integralmente |
| `.local/toolchains` | 4,34 | Preservar ferramentas instaladas |
| `.local/preserved` | 3,60 | Preservar backups e material de finalidade incerta |

As maiores entradas individuais são as ISOs originais de Forza Horizon e
Forza Horizon 2, 8,74 GB cada, ambas preservadas. Várias cópias do arquivo de
pista do Forza, com 2,94 GB cada, explicavam parte importante da multiplicação.
O problema era majoritariamente local/ignorado, não arquivos versionados inúteis.

### Resultado medido

| Medida | Antes | Depois | Redução / aumento livre |
|---|---:|---:|---:|
| Checkout, soma lógica | 179.011.082.716 bytes (179,01 GB / 166,72 GiB) | 120.359.082.573 bytes (120,36 GB / 112,09 GiB) | **58.652.000.143 bytes (58,65 GB / 54,62 GiB), 32,76%** |
| Espaço livre no volume C: | 24.529.485.824 bytes | 82.438.590.464 bytes | +57.909.104.640 bytes (57,91 GB) |

35.174 arquivos removidos somam 58.785.588.695 bytes, além de um diretório vazio.
A redução líquida inclui o custo dos inventários e registros locais de auditoria
(aproximadamente 134 MB), novos documentos e metadados Git. A variação do volume
é observacional: inclui alocação/compactação NTFS e atividade de outros processos,
não deve ser igualada à soma lógica dos arquivos. Valores representam o instante
da conferência final; pequenos registros e metadados do commit podem crescer depois.

O checkout permanece acima de 100 GB porque originais, biblioteca, seeds,
evidências e ambientes históricos foram preservados. Não se remove conteúdo
incerto para atingir um tamanho arbitrário.

## Exclusões verificadas

- 23.012 arquivos `.obj`, `.pch`, `.lib`, `.pdb`, `.ilk`, `.exp` e `.idb` sob
  builds locais e builds isolados de distribuição: **21.321.223.589 bytes**.
- Cinco cópias descartáveis do conteúdo extraído de Forza, em quatro resultados
  de benchmark e um harness Xenos X2: **12.160 arquivos / 36.602.307.635 bytes**.
  Cada arquivo foi comparado por tamanho e SHA-256 com a extração original;
  zero divergências. Nenhum perfil, save, header de catálogo ou cache entrou
  nessa seleção. A seed imutável dos benchmarks foi mantida integralmente.
- Cache baixado do LLVM 22.1.8: **862.053.924 bytes**, hash conferido com o lock,
  após confirmar que o compilador instalado responde com a versão pinada.
  URL e checksum para novo download continuam em `deps.lock.json`.
- Diretório vazio `pipeline-store-test-35340` e proposta de licença obsoleta.

As exclusões de arquivos usaram caminhos absolutos conferidos dentro do
checkout, sem atravessar reparse points, com tamanho/data novamente validados.
O inventário, plano, SHA-256 das cópias e registro por arquivo removido ficam em
`.local/cleanup-audit/`, sem publicar caminhos privados nem conteúdo original.

## Preservação, recuperação e validação

Originais/importações, perfis/saves, catálogos, biblioteca ativa, LLVM instalado,
SDK pinado, seus 76 patches, licenças, worktrees, bundles/backups, logs e capturas
foram preservados. `.local/tmp` contém módulos/fontes e experimentos de origem
incerta; não foi tratado como descartável pelo nome. O mesmo vale para snapshots
históricos, dumps gráficos e arquivos de referência. Não houve `git clean`,
`reset`, expurgo de objetos, alteração de pin ou exclusão por extensão global.

Os intermediários serão recompilados pelo build normal; os PDBs removidos
precisam de rebuild/relink para depuração. Os executáveis/DLLs existentes foram
mantidos, sem nova certificação funcional. As cópias descartáveis de conteúdo
podem ser recriadas a partir dos originais; benchmarks novos usam a seed
preservada e novos labels, sem alterar resultados históricos.

A comparação antes/depois não encontrou nenhum arquivo ausente fora da lista
de exclusões, nem mudança de tamanho fora dos documentos/ignore, metadados Git
e registros desta auditoria. Os 320 arquivos de perfis/saves/catálogos mantiveram
seus SHA-256.

Verificações pertinentes: hashes de 320 arquivos de perfis/saves/catálogos,
`apply-sdk-patches.ps1 -Check`, checker de contribuições com PyYAML 6.0.2 já
disponível localmente, links Markdown, whitespace e regras de ignore. Nenhuma
suíte nativa, build ou regressão gráfica foi repetida para esta mudança documental.

Publicação segue PR e `Contribution checks`, sem bypass das proteções da main,
force-push, tag ou Release. O relatório não autoriza apagar os itens preservados
em uma limpeza futura sem verificar novamente sua finalidade.
