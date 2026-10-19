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

## Complemento de integração

`8c258024ee6d884816bd62b518ff9b604d029352` contém a unificação. O ajuste
seguinte, `44cd00f`, mantém a importação vinculada exatamente à origem selecionada:
um XEX isolado não autoriza importar outro `default.xex` da pasta ao lado.
O usuário deve selecionar a pasta completa ou um contêiner reconhecido. Duas
asserções adicionais cobrem a recusa sem criar registro de importação.

`receipt-impact.json` compara os arquivos alterados com inputs/configuração/
produtor dos sete recibos anteriores: nenhum input registrado foi alterado,
e os SHA-256 dos recibos permanecem iguais aos do relatório anterior.
Não houve rebuild de títulos nem repetição de suas regressões.

O mantenedor iniciou as dashboards manualmente enquanto a distribuição era
compilada. A leitura dos registros demonstra somente os seguintes fatos:

- `20261019T124722Z-session-0-dash_17559`: processo iniciado, saída 0 após
  111.565 ms e zero linhas críticas. O bloqueio do Castle não ocorreu.
- `20261019T124925Z-session-0-dash_1888`: processo iniciado, encerramento por
  `STATUS_STACK_BUFFER_OVERRUN` após 48.911 ms. O log registra Guide/criação
  de perfil e chamada não resolvida de `0x92164098` para `0x92164008`, após
  descarregar `createprofile.xex`. Causa do runtime e correção continuam
  pendentes; esta evidência não aprova Guide/perfis da 1888.

Essas sessões foram abertas pelo mantenedor, não pelo agente. Não equivalem a
aceite visual explícito nem à certificação de todos os itens do roteiro anterior.

## Entrega final

Após o mantenedor encerrar as janelas, o build do atalho foi reconfigurado e
recompilado em Release (`local-configure-final.txt`, `local-build-final.txt`,
saída 0), com versão de desenvolvimento `0.56.8-dev`. O caminho impresso por
`scripts/start-desktop.cmd --print-path` é
`.local/build/desktop-only/src/desktop/x360-native.exe`.

- Frontend SHA-256: `ed05dd84d6df5af736ed2e6a4148a5fd9f64a99cb13a99c9ad1b18cc1156e7d4`.
- Supervisor adjacente SHA-256: `21021a859a4b35819f2665d1bc8817d9ecab401f787238f54f4caee79b8cf41d`.
- Regressões finais: `model-final.txt`, 30 asserções / 2 casos;
  `frontend-final.txt`, 8 asserções / 1 caso, aprovadas.

O candidato final 0.56.8 foi construído de
`44cd00f800a3fa4ec26ae5c2d7daecfb86de611a`, em árvore limpa de distribuição,
Release. `package-final.txt` registra aprovação de `desktop.catalog` e
`desktop.update-recovery`, além dos gates de conteúdo/licenças (89 componentes,
3 executáveis). O ZIP foi extraído e seus índices, conteúdo, licenças e
configuração foram conferidos novamente em `zip-gate.txt`.

Arquivo: `.local/packages/library-membership-20261019-final/x360-native-0.56.8-windows-x64.zip`.
Tamanho: 8.792.674 bytes. SHA-256:
`385b1c1c10749c2c1df7cf27b498a7b1a0cadf094493dc15a721b5def415b820`.
O candidato intermediário 0.56.7 foi preservado, substituído pelo 0.56.8.
Nenhum pacote foi publicado ou aplicado como atualização de uma instalação real.
Windows limpo e experiência visual do pacote continuam sem certificação.
