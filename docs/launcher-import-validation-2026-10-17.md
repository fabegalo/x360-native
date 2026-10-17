# Validação do launcher: dashboard 17559 e Motocross (2026-10-17)

## Escopo e resultado

Validação focada do checkout Windows x64 em `8c0f84b`, sem sessões de jogo ou Guide. Gameplay e Guide já estavam validados conforme a solicitação. Não houve alteração de código, preparação de conteúdo, reimportação sobre os dados reais ou mudança de perfis/saves. Builds e testes temporários usaram `.local/test-temp` para manter gravações temporárias dentro do workspace.

O launcher passou nos fluxos focados de integridade, reimportação idempotente, recibos AOT e registro da My Games. Os originais e a instalação existente do Motocross foram lidos e comparados; nenhum arquivo real foi sobrescrito.

## Conteúdo real conferido

- Dashboard Retail 17559: o teste `local.Dashboard 17559 original dependency hashes match the active package` aprovou a identidade/versão e os hashes do conjunto ativo. O `system/xam_ui.xex` presente em `.local/inputs/dashboard/17559` bate com o SHA-256 `78b3dbd8ae866f4258f5444205486ea9f8451afaa30f469c875dcf345f828467` registrado em `titles/dash_17559/preparation.toml`.
- AvatarAssetPack: o `avatar/AvatarAssetPack.toc` bate com `a8c90b3db5c6349a55fcb72eda47e08e4940f88fabda4ff73025d9728832daa8`. O teste local confirmou que o catálogo 17559 é estruturalmente legível.
- Motocross: `default.xex` bate com SHA-256 `2a8406bcf9c223f9d7da732be617cc7cd46fbb587ef552f7fcacac2049b95dcd`. O perfil local do título e o perfil da dashboard foram carregados em modo de validação estrita dos builds/recibos; o teste de perfil/avatar terminou com 10 asserções aprovadas.
- My Games: a instalação existente em `.local/user/dash17559/0000000000000000/58410B0F/000D0000/7E38A2BCC89C941C7C6BA32DD79E873F42333EF658` contém os 54 arquivos do inventário do Motocross. Comparação SHA-256 origem → inventário → instalação: 54/54 iguais, 0 ausentes, 0 divergentes (1.558.376.130 bytes). `default.xex` instalado também corresponde ao hash listado acima. O header de 328 bytes em `Headers/000D0000/7E38A2BCC89C941C7C6BA32DD79E873F42333EF658.header` corresponde byte a byte ao registro esperado para Title ID `58410B0F`, content type `000D0000` e o nome de conteúdo Motocross Madness.

## Testes executados

- Build focado via `scripts/devshell.cmd`: `x360n_tests`, `x360n_local_tests`, `x360n_desktop_tests` e `x360n` concluído.
- `desktop.catalog`: 53 casos / 656 asserções aprovados. Cobriu importação/reimportação, preservação de destino alterado, entrada opcional ausente/divergente, recusa de conjunto misto de versões, recibos AOT e registro My Games com publicação do header por último.
- `x360n.dashboard-assets-build-gate`: aprovado; recusou bytes de versão errada, dependência ausente e hash não identificado.
- `x360n.preparation-receipt-publication`: aprovado.
- `x360n_local_tests.exe`: 10 casos / 2.528 asserções aprovados, incluindo inventário dos originais locais, hashes/versão 17559, parser do AvatarAssetPack, perfil de avatar Motocross/dashboard e recusa de outra versão de dashboard.
- Testes focados do supervisor sobre dependências de dashboard ausentes/divergentes e arquivos de avatar por versão/bytes: 10 e 20 asserções aprovadas. Teste de recibo ausente, parcial, título errado ou incompleto: 6 asserções aprovadas.

A primeira execução de `desktop.catalog` usou o `%TEMP%` padrão fora da área gravável e expirou com erros de acesso Windows 5. Repetida com `TEMP`/`TMP` apontando para `.local/test-temp`, passou; não foi reproduzida como falha do launcher.

Um teste auxiliar fora do escopo solicitado (`local receipts keep Castle Crashers dashboard and Iruka ready`) falhou em duas expectativas próprias de outros títulos: Castle Crashers não estava preparado segundo aquele caminho auxiliar e Iruka tinha recibo válido onde o teste esperava não ter. A asserção da dashboard passou; o teste focalizado do perfil Retail 17559 e do recibo Motocross passou. Nenhum desses estados de Castle/Iruka foi alterado neste trabalho.

## Conclusão

Não foi reproduzida falha na importação/verificação de 17559, XAM, AvatarAssetPack, Motocross, recibos AOT ou registro da My Games. Não houve correção de código necessária. Os dados reais foram preservados; mudanças persistentes limitam-se a este relatório versionado. Sem push ou Release.
