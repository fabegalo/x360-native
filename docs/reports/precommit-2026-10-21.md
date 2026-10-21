# Validação das alterações locais — 21/10/2026

O mantenedor solicitou validar as alterações locais e fazer commit. Esta rodada
abrange conquistas offline, notificações originais 17559, launcher/My Games,
tela cheia, importação, entrada START e bloqueio do Guide durante a intro.
Commit local autorizado; nenhum push, tag ou Release solicitado nesta rodada.

## Resultados

Evidências locais em `.local/evidence/validation/precommit-20261021/`:

- Build Release `dash_17559`: concluído, incluindo o alinhamento do boot,
  com novo recibo de preparação. Etapa nativa: 16 segundos; não é benchmark.
- Build Release `x360n_sdk_runtime_checks`: concluído.
- Build Release `x360-native`, `x360n_desktop_tests` e `x360n_launcher_tests`:
  concluído (produtos já atuais).
- Runtime: 1.867 asserções, 28 casos aprovados. Três casos condicionais
  inicialmente omitidos foram executados separadamente, conforme abaixo.
- Metadados originais do Forza Horizon e isolamento da ferramenta: 667 asserções,
  dois casos aprovados; leitura do XEX sem executar código do jogo.
- Persistência: processos independentes de gravação e leitura aprovados,
  dois perfis e dois jogos isolados; hashes dos arquivos preservados na leitura.
- Modelo do launcher/importação/renderização: 95 asserções, cinco casos aprovados.
- Interface do launcher, prontidão, layout, digest e tela cheia: 63.969 asserções,
  13 casos aprovados.
- Verificação do SDK: todos os 112 patches ativos aplicados e hashes conferidos.
  Pin preservado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
- Os 12 artefatos do recibo final da 17559 tiveram seus hashes conferidos.
- Verificação de whitespace dos fontes/documentos: sem erros após remover
  uma linha vazia final do relatório de sessão. Os arquivos `.patch` preservam
  espaços obrigatórios de contexto de diff; a checagem genérica os sinaliza,
  mas eles não foram removidos nem seus hashes alterados.

Filtros usados: runtime `[offline-achievements],[offline-achievement-api],
[offline-achievement],[achievement-ui],[achievement-tool-isolation],
[signin-task],[guide-opening],[input-p0]`; metadados
`[local-achievement-metadata],[achievement-tool-isolation]`; desktop
`[launcher-flow],[import-path-termination],[renderer]`; launcher
`[launcher-options],[launcher-ready],[digest-batch],[session-fullscreen],
[launcher-layout]`. Persistência pelo script
`tests/local/offline-achievements-process.ps1`.

## Limites do aceite

Nenhum jogo/dashboard foi iniciado nesta validação e nenhuma janela do usuário
foi controlada. Perfis e saves existentes foram preservados. A confirmação
anterior do mantenedor cobre dashboard/My Games/Forza, mas **a exibição visual
da conquista após a última correção ainda precisa de aceite real**. Os testes
técnicos não substituem gameplay. Tela cheia e START físico continuam com
confirmação manual pendente; os crashes documentados em STATUS não foram
considerados corrigidos.

O aceite restante de conquistas é entrar com o perfil local, obter uma conquista
real ainda bloqueada, observar a notificação original, conferir o Gamerscore no
Guide e reiniciar para verificar persistência. A autorização de commit não
altera essas limitações. Relatórios anteriores preservam resultados e hashes
históricos; este documento registra a validação da rodada de commit.
