# Biblioteca única no launcher — 19/10/2026

## Achado e reprodução

Sobre `8d1e86af4897a191d3b39d686d576995fc541861`, o mantenedor relatou
que o launcher abria, mas a dashboard não iniciava. Os registros locais
`139313296` a `139716078`, em `.local/evidence/logs/desktop`, recusaram a
sessão com `Prepare para registrar o conteúdo verificado na My Games:
castle_crashers`. O mantenedor confirmou a remoção manual de dados locais.
As pastas `.local/user` e `.local/catalog` estavam ausentes.

O catálogo de compatibilidade era confundido com a biblioteca do usuário:
o frontend considerava um jogo adicionado pela presença de seu XEX; o preflight
exigia registro na My Games pela presença de seu build válido. A remoção,
por outro lado, exigia um registro de importação. Castle, Motocross e Forza
podiam reaparecer como itens fixos e bloquear a dashboard após a limpeza.

## Correção

O registro persistente de importação é a única autoridade de participação de
um jogo na biblioteca. `InGameLibrary` é compartilhado pela apresentação,
remoção, registro My Games, preflight e seleção de rotas da sessão. Arquivos
originais e builds remanescentes não adicionam jogos automaticamente.

As receitas continuam identificando conteúdo exato e fornecendo compatibilidade;
não são instalações. Jogos com receita própria usam o mesmo ImportGame e
RemoveGame dos demais. A importação alternativa pela identificação de arquivos
também passa por ImportGame. Após remover, a receita e um build remanescente
não fazem o item reaparecer. Reimportar restaura a mesma identidade.

O supervisor vincula as rotas de uma dashboard com biblioteca ao mesmo catálogo
importado, sem executar receitas soltas. Sessões explícitas sem biblioteca
preservam seus contratos. SHA-256, recibos, isolamento das dashboards e
validação antes da execução continuam obrigatórios. Nenhuma mudança no SDK,
nas receitas dos títulos ou nos binários dos jogos foi necessária.

Não houve importação automática, recuperação de saves nem exclusão de dados
reais nesta correção. Com o catálogo apagado pelo mantenedor, a biblioteca fica
vazia até ele importar os jogos desejados, como no fluxo de usuário final.

## Validação executada

Windows x64, Release, LLVM 22.1.8, builds sequenciais por `scripts/devshell.cmd`.
Evidências locais: `.local/evidence/library-membership-20261019/`.

- `build.txt`: frontend do atalho, supervisor e testes compilados; saída 0.
- `model-tests.txt`: 473 asserções / 27 casos de importação, remoção, preflight,
  registro original, caminhos longos e isolamento da biblioteca aprovados.
- `frontend-tests.txt`: 41 asserções / 3 casos aprovados; visibilidade e remoção
  usam a mesma participação para jogos com receita e AOT genérico. As sessões
  dos testes usam filhos sintéticos, sem dashboard ou jogo real.
- Após ampliar a regressão da remoção para conferir a cópia My Games e preservar
  o perfil, `build-tests-final.txt` passou e `recipe-removal-final.txt` aprovou
  28 asserções / 2 casos. A fonte original, o save e a identidade da reimportação
  foram preservados.
- `preflight-17559.txt`: o verificador somente de leitura conferiu o ambiente
  real com saída 0 e `library_launch_targets=0` (3,09 s no total). Essa ferramenta
  não cria processo de título nem janela. Não é medição de startup/gameplay.

A revisão automática inicialmente recusou o último comando por incerteza sobre
execução guest. A leitura integral de `launch_benchmark.cpp`, do target CMake e
do registro de compilação demonstrou que somente lê/verifica arquivos e imprime
JSON; a mesma chamada foi então aprovada. Nenhuma dashboard/jogo foi iniciada.

O aceite visual permanece manual. O candidato 0.56.6 do relatório anterior
antecede esta correção e não deve ser tratado como contendo o novo fluxo.
Sem push, tag ou publicação.
