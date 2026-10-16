# Games Played e licenças XBLA — 16/10/2026

A partir de `2.0.6683.0`, iniciar conteúdo Arcade Trial não acrescenta seu
Title ID aos jogos do perfil. Versões anteriores continuam registrando Trials.
Jogos completos e conteúdo com licença ativa continuam elegíveis. A regra é
compartilhada, sem exceção por jogo, e só afeta novas inclusões: não apaga nem
reclassifica registros anteriores.

## Implementação

O supervisor lê `dashboard_version` do `preparation.toml` e passa a versão da
dashboard da sessão a cada processo, inclusive ao jogo e no retorno. O argumento
é reservado, e versões malformadas são recusadas. A consulta ocorre no handoff,
sem trabalho por frame ou instrumentação permanente.

`titles/common/games_played.h` identifica o pacote instalado pelo caminho e pelo
Title ID do XEX carregado, inclusive entrypoints em subdiretórios. Lê a máscara
anexada ao cabeçalho agregado de 0x148 bytes, com a mesma ordem de bytes usada
pelo ContentManager do SDK. Cabeçalho ausente, curto ou divergente não concede
licença a conteúdo identificado como Arcade. O runtime também consulta a máscara
ativa que `XamContentGetLicenseMask` expõe ao jogo, para respeitar Full desbloqueado;
não altera essa máscara nem fabrica licenças. Sem versão conhecida, Trial
identificado não é registrado e produz diagnóstico explícito. Títulos sem pacote
Arcade identificado mantêm o caminho anterior.

Persistência, deduplicação, seleção de conta e exclusão de títulos de sistema
continuam no `RecordTitlePlayed` existente. Não houve alteração no SDK nem no
commit de idioma/região `045857d`.

## Evidências e limites

Foram aprovados **11 casos focados distintos**: parsing e limite da versão,
Trial/Full/licença ativa/disco, metadados inválidos, origem da versão e argumento
reservado, propagação por processos, licença instalada real, GamerCard XAM,
identidade da sessão, idioma/localidade e dois testes de isolamento de módulos.
Não foi executada suíte completa.

O teste `real installed Arcade license controls Games Played in both dashboard
profiles` carrega os metadados do XEX real de Castle Crashers (`584108B7`) em
`tool_mode`, chama o `ImportLaunchData` de produção e usa perfis novos e isolados.
Para cada `preparation.toml` de **6717 e 17559**, verifica Trial sem incremento,
repetição idempotente, outra conta intacta, inclusão em `2.0.6682.0`, preservação
desse registro ao voltar à versão nova e leitura após reabrir o armazenamento.

O pacote local tem cabeçalho de **328 bytes**, sem trailer de licença, portanto
máscara **0**; a máscara ativa também permaneceu **0**. Não foi forçado Full.
SHA-256 do cabeçalho, igual antes e depois da validação:
`3b5f5f95911839ccd0227af47c742dac659e2542d4d141cf9854ed4d37063b7c`.
XEX conferido contra o manifesto:
`825e9c8cb951d62ce954901488c5b783349c98cc5c03bd08ea0739df3af5d9ad`.
Full foi coberto com dados sintéticos; não há certificação de licença comercial
Full neste ensaio.

A primeira execução teve 6/7 casos aprovados; o teste real falhou criando perfil
no TEMP longo do sandbox (erro 1627). Com TEMP curto passou. O teste foi então
ajustado para usar diretório único no diretório de trabalho, como os testes de
GamerCard, evitando essa dependência do TEMP. A execução final passou com TEMP
normal (0,41 s para o caso que cobre as duas versões).

Builds aprovados: `x360n_tests`, `x360n_sdk_runtime_checks`, `dash_17559` e
`castle_crashers`. O build **6717 foi bloqueado corretamente** pelo gate:
`XenonJKLatin.xtt has no verified matching version/hash; no fallback`.
Não foram emprestados recursos de outra versão, renovados recibos de preparação
nem executados binários antigos após essa falha.

Assim, a evidência para 6717 e 17559 cobre a regra e os contratos de runtime,
**não uma sessão visual das dashboards**. Sem Computer Use, execução de gameplay,
push ou Release. Não foi feita alegação de ganho de performance.

## Reprodução focada

Compilar via `scripts/devshell.cmd cmake --build --preset win-amd64-debug
--target x360n_tests x360n_sdk_runtime_checks`. Somente após sucesso, definir
`X360N_TEST_ARCADE_IMAGE` para o `default.xex` de um pacote instalado e executar
CTest com `-R "real installed Arcade" --timeout 60`. O teste é pulado se a variável
não existir, e recusa máscara ativa diferente de zero. Nenhum original é copiado
para o repositório ou modificado pelo teste.

Logs locais em `.local/logs/games-played-*.log`: builds, primeira rodada de sete
casos, repetição da licença real, regressões de sessão/idioma e isolamento.
