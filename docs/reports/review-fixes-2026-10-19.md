# Correções da revisão de entrada, distribuição e atualização

Base desta tarefa: `477164b8c468714d6fb07e8011aba94776c77fa4`, na `main` local.
O commit posterior à referência auditada modifica a biblioteca; não corrige os
achados aqui tratados. A árvore principal estava limpa, com os patches esperados
no submódulo. Não houve execução de dashboards/jogos nem Computer Use.

Revisão de implementação corrigida: `555b5bfd88f425f9fd5155425d14cdca211dc668`.
Commits locais separados: `6248a83` (entrada), `48f53df`/`be475a1`
(distribuição), `dbc78b7`/`d4077b3` (atualizador), `555b5bf` (precedência da
seleção explícita de chaves). Sem push, tag, Release ou reescrita do histórico.

## Entrada — correção e evidência nova

| Achado | Reprodução | Correção | Teste e resultado | Limite |
|---|---|---|---|---|
| DOWN pendente após desconexão | Teste integrado no backend real do SDK retornou sucesso em vez de 1167 | Patch 0102 descarta estado na perda/troca da conexão | Release: reprodução com 3 falhas; regressão final aprovada | Controle físico ainda pendente |
| Pendência sobrevive a shutdown/backend e outro runtime | Dois testes sem drenar artificialmente KEYUP reproduziram o vazamento global | Estado pertence à instância de entrada; Shutdown, atribuição e identidade invalidam pendências e posse | `[guide-lifecycle]` integrado | Morte de um processo encerra sua memória; não há estado de entrada persistido |
| GetState contorna o atraso | Primeiro BACK visível enquanto o evento aguardava | Decisão compartilhada de janela entre estado/eventos | Quatro índices, atalho, repetições, soltura e desativação com pendência | Janela é heurística opt-in |
| ANY_USER consultava apenas índice 0 | Backend sintético com eventos em cada índice | Busca dos quatro índices, respeitando o dono da aplicação de sistema | ANY_USER do título/HUD Ex aprovado | Estado/capacidades preservam sua ABI existente |
| Atraso automático em todas as dashboards | Defaults de 150 ms nos dois hosts | Removidos defaults; `guide_chord_ms=0` permanece padrão | Índice não selecionado e Guide dedicado imediatos | Não há calibração física dos 150 ms |

Configuração explícita para um controle que emula Guide por BACK+START:
`--guide_chord_ms=150 --guide_chord_users=1`. A máscara seleciona índices guest:
1, 2, 4, 8; 15 seleciona todos. O alcance é o usuário selecionado e seus
consumidores de entrada XAM, inclusive HUD, para que um consumidor não contorne
o outro. Dispositivos mesclados no mesmo usuário compartilham essa política;
não habilitar a janela em usuários que não precisam do atalho. Limite aceito:
0–1000 ms. Sem sleeps no runtime. Desativar libera a pendência na ordem original.
Para uso pelo launcher, as mesmas opções podem ser escritas no topo do arquivo
`dash_1888.toml` ou `dash_17559.toml`, ao lado do executável correspondente:
`guide_chord_ms = 150` e `guide_chord_users = 1`, sem seção `[Input]`.
Alternativamente, o runtime aceita `REX_GUIDE_CHORD_MS` e
`REX_GUIDE_CHORD_USERS` no ambiente do processo. Nenhuma configuração persistente
do usuário foi alterada nesta tarefa.

Execução nova Release: **500 asserções / 18 casos** com filtros
`[guide-lifecycle],[guide-chord],[guide-opening],[guide-input],[profile-ui]`.
Logs: `.local/evidence/review-fixes-20261019/input-red.txt` e `input-final.txt`.
A primeira tentativa verde detectou um acesso nulo em runtime sem backend;
corrigido e repetido com sucesso. O primeiro wrapper do build ficou sem progresso
após link; encerrado e repetido com limite explícito. Nenhum teste sobre build
fracassado foi contado. Não há alegação de ganho de performance.

## Distribuição e chaves legadas — evidência nova

O 0095 ativo agora lê material legado de arquivo local, como o extrator já fazia,
em vez de incluir valores no patch distribuído. O supervisor e o driver oficial
usam `.local/keys/xex-keys.txt`; `X360N_XEX_KEYS`/`--xex_keys_file` permitem seleção
explícita. A preparação registra o SHA do arquivo usado, sem copiar seus bytes.
O supervisor respeita a variável explícita antes de oferecer o arquivo padrão.
Três sondas CLI com filho sintético aprovaram padrão/ambiente/argumento
(`key-selection-verified.txt`). As duas primeiras tentativas da sonda falharam
no parser do relatório e na comparação literal de separadores Windows; o parser
foi corrigido, sem alterar o produto para contornar as verificações.
Ausência, entrada malformada, nome duplicado e chave necessária ausente geram
falha explícita. O histórico publicado não foi reescrito.

O gate usa fingerprints irreversíveis de exclusão e verifica bytes, texto C++ e
diffs com prefixos de adição/remoção/contexto. Esses fingerprints servem apenas
para rejeitar material; não são usados para carregar XEX nem recuperar chaves.
Fixtures sintéticos cobrem esses formatos sem incorporar constantes reais.
Os testes do carregador fornecem chaves legadas sintéticas por arquivo local.

Reaplicação nova: **101 patches / 108 arquivos finais** coincidem com o lock em
cópia descartável do SDK e SDL fixados, sem edições ocultas no SDK real.
Log: `sdk-replay.txt` na pasta de evidências desta tarefa.
Contratos Release combinados de entrada, chaves/recursos e banner:
**1.324 asserções / 22 casos** (`contracts-final.txt`).
Pacote exportado e ZIP ainda aguardam o fechamento abaixo.

## Atualizador — evidência nova

| Achado | Reprodução/contrato | Correção | Teste executado | Limite |
|---|---|---|---|---|
| Rollback dependia de catch/lista em memória | Encerrar o processo em cada fronteira de rename | Plano imutável persistido antes de mover, cópias verificadas, renomes duráveis e recuperação idempotente | Processo novo recupera todos os pontos de interrupção de aplicação e rollback | Reinicialização elétrica física não executada |
| Launcher ausente ou versão nova não inicia | Launcher sintético movido; substituto é arquivo inválido | Supervisor anterior copiado fora dos arquivos movimentados; `recover-update.cmd` independente | Recuperação pelo executável salvo aprovada nos dois cenários | Instalação real não atualizada |
| Inventário/caminhos não validados antes dos renomes | Pacote alterado, traversal, DATA, arquivos extras, colisão local | Verificação integral dos dois índices, recusa de redirects e colisões; movimentação somente de arquivos inventariados | Fixtures rejeitados com aplicação/dados preservados | Sem atualização por rede nos testes |
| Falha de rollback era ignorada | Arquivo bloqueado durante recuperação | Erro explícito, diário e backup preservados | Nova tentativa após liberar o arquivo restaura e verifica o pacote | Contenção de software externo real não ensaiada |
| Estável recebia pré-release | Tags rc e flag prerelease do GitHub | Canal estável padrão; preview por adesão explícita | Seleção sintética aprovada | Nenhuma consulta ao GitHub feita |

Release final: **1.263 asserções / 11 casos**, incluindo interrupções determinísticas,
retomada em novo processo, rollback interrompido, helper independente e preservação
de perfis/saves/importados/arquivo local não gerenciado. `update-final-closure.txt`.
Inclui colisão com script de recuperação local e duas atualizações consecutivas:
a primeira precisa ser confirmada, os dois backups são preservados e a segunda
transação pode restaurar a versão intermediária. O aviso de transação incompleta
no frontend independe da descoberta bem-sucedida de app/workspace.
Recuperação manual conserva a transação para diagnóstico e não apaga versões
anteriores. O início da versão nova confirma o pacote após criar sua janela;
o backup continua disponível. Canal preview: arquivo `data/update-channel.txt`
com a linha `preview`; removê-lo retorna ao canal estável.

Adaptadores próprios 1888: **51 asserções / 1 caso** em Release. Firmware routines
1, 5 e 2: **3 sondas aprovadas**, handoff 6 e um callback em cada. A primeira
invocação apontou incorretamente ao runner de testes; recusada com exit 2, sem
execução de título, e repetida com a sonda correta. Isso comprova encerramento
organizado, não reboot fiel do console.

Fechamento Release adicional: **3.315 asserções / 16 casos** em
`[dashboard-assets],[dashboard1888-ui],[xex-patch]`, preservando isolamento dos
originais, ABI da 1888 e a correção anterior de apply-patch. O fixture CR foi
removido da saída gerada, regenerado pelo rexglue atual, compilado e aprovado:
**24.576 verificações PPC** de crmove/crnot/crset/crclr. Logs:
`final-focused-build.txt`, `isolation-patch-release.txt`, `cr-alias-release.txt`.

O frontend Release da árvore principal compilou. A tentativa de atualizar o
frontend do atalho em `desktop-only` falhou no link porque esse executável estava
aberto (PID 3468). Nenhum teste sobre esse build falho foi contado. O mantenedor
confirmou o fechamento; o novo link e o build da árvore principal passaram
(`desktop-shortcut-retry.txt`, `supervisor-final-build.txt`), sem abrir a janela.

## Recertificação e aceite

Preparação oficial, sequencial, Windows x64 Release, LLVM 22.1.8 e SDK
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. Os recibos anteriores de Motocross,
Castle, Forza e dos dois AOT genéricos incluem hashes antigos de
`input_system.cpp`, `xam_input.cpp` e `build-title.ps1`; portanto são consumidores
invalidados, não uma ampliação de cobertura (`invalidated-consumers.txt`).
Os sete consumidores receberam codegen e recibos novos. No Motocross,
o codegen executou todos os módulos (zero módulos pulados): 666 saídas idênticas
ao conteúdo anterior, sem escrita desnecessária; runtime e produtos nativos
foram reconstruídos. Isso verifica regeneração, não gameplay nem suporte às
instruções gráficas que o codegen ainda sinaliza nos logs. A 6717 continua bloqueada
historicamente por fonte sem identidade verificada; não foi reexecutada nem
recebeu arquivo de outra versão. Iruka permanece removido pelo mantenedor.

Conferência final independente: todos os hashes de entradas, produtos,
configuração, produtor, estado incremental e inventários por glob coincidem;
nenhum marcador incompleto ficou nos sete produtos. Cache e comandos de
compilação conferidos em Release. Logs em `product-receipts.txt` e hashes
completos de executáveis/recibos no [registro local](../../.local/evidence/review-fixes-20261019/product-receipts.json).

| Produto | Entradas | Artefatos | SHA-256 do recibo |
|---|---:|---:|---|
| 1888 | 3733 | 10 | `671694a3f97521cf25022d2c403cbff776c3218f0b63af59f97d7fa64cf2417b` |
| 17559 | 3971 | 12 | `86c1d6487a9d3f95601e1fe00ce4c45cfdf86103ec76ac012f44e86a86184eda` |
| Motocross | 4125 | 5 | `a2cb85bfeb1227ba1f726e861314d073b6bcbcaafed8d8d8cc1a61abaaeeae1d` |
| Castle | 3475 | 4 | `41716c2529326af91227966de37d573855df2475a710ff5103a88f86078ac397` |
| Forza | 4061 | 6 | `4d5537ccdfe2fca0c8e77884ba0365ab1b89a7886ae694874ac3a2999b8796c3` |
| Charlie (`g0a4c6253b34297dc`) | 4005 | 34 | `8a52d8cca0d40f335e5fd8a825aa40d4c7463c6aaefdf00529d6e65196b98245` |
| `4D530A81` (`g6ad318fe6b4d245a`) | 3449 | 3 | `45cbd25b447c56bf5bbe21443d91e445b94348ce14ffd6d67eeb9786d93fa750` |

Roteiro reservado ao mantenedor, nos novos produtos 1888 e 17559:

1. Abrir cada dashboard e conferir intro, áudio e chegada à interface original.
2. Navegar por teclado e controle físico; usar BACK e START separadamente,
   inclusive pressionamento longo, repetição e soltura.
3. Abrir/fechar o Guide várias vezes, inclusive após fechar uma aplicação de
   sistema; conferir que comandos normais continuam chegando na ordem correta.
4. Testar Guide dedicado com a janela padrão desligada. Para controle que usa
   BACK+START, habilitar explicitamente a configuração acima e testar o atalho,
   BACK/START separados e os índices de controle disponíveis.
5. Desconectar com BACK/START pendente, reconectar e conferir ausência de comando
   antigo. Encerrar e reabrir cada dashboard, repetindo a operação do Guide.

Nenhuma etapa desse roteiro foi executada pelo agente. Build/recibo e testes
sintéticos não certificam a experiência gráfica, sonora ou o controle físico.

### Pendências desta tarefa

Pacote candidato e aceite manual ainda em andamento. Evidências históricas do
relatório de Guide não são recertificação desta tarefa. Encerramento organizado
continua distinto de reboot fiel do console.
