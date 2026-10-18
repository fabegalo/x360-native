# Falha de preparação da dashboard no launcher — 2026-10-18

A preparação Retail 17559 iniciada no launcher terminou em `4-build`, saída 1,
às 09:31 (America/Sao_Paulo). Configure, geração AOT dos módulos e reconfigure
haviam concluído. O erro determinante foi a linkagem de
`dash_17559_session_ui.dll`: `undefined symbol: x360n::TryBatchDigest` chamado
por `DigestFile` em `src/supervisor/sha256.cpp:114`.

O commit `6e535cf` introduziu essa chamada e adicionou `digest_batch.cpp` ao
supervisor, mas os targets de títulos que compilam `sha256.cpp` diretamente
continuaram sem a implementação. O aviso de redefinição de
`WIN32_LEAN_AND_MEAN` e o aviso de import variável não são a causa dessa falha.

A correção inclui `src/supervisor/digest_batch.cpp` nos dois targets da 17559
(executável e plugin de interface de sessão), assim como nos consumidores
diretos da 6717 e do Forza. Não muda o algoritmo de hash, o contrato de
verificação, o SDK, módulos originais, perfis ou saves.

Evidência original preservada:

- `.local/evidence/logs/desktop/52737500/dash_17559.log`
- `.local/build/titles/dash_17559/x360n-build-4-build.5a64506aab4744c4bee7e22fcc49228a.log:1817`

Validação da correção: preparação da 17559 pelo driver oficial
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-title.ps1 -Title dash_17559`.
A primeira tentativa de validação, identificador
`53a9a2c27f19434bacbfa5a6c7d9707e`, foi interrompida: a ferramenta compilou,
mas o PowerShell que envolvia o Ninja ficou sem filhos e sem avançar após sua
saída. Apenas os processos dessa tentativa foram encerrados. Essa saída 1 foi
provocada pela interrupção do wrapper; não foi uma nova falha do linker.

A preparação foi repetida fora do sandbox, com logs em
`.local/build/titles/dash_17559/x360n-build-*.6af8310ea4334db4b4ba92331fd70cce.log`.
Configure (11 s), ferramenta (109 s), codegen principal (38 s), quatro
manifests adicionais (3 s cada), reconfigure (12 s) e build final (166 s)
terminaram com código 0. O novo log confirma a linkagem de
`dash_17559_session_ui.dll` e de `dash_17559.exe`, sem o símbolo ausente.
O driver terminou com saída 0 e publicou o recibo às 09:52:26; o estado
incremental foi publicado às 09:52:20. Conferência posterior: os 13 artefatos
e os inputs corrigidos coincidem com seus hashes; o marcador
`.local/preparation/dash_17559.incomplete` está ausente.

Enquanto essa preparação possuía a trava, o mantenedor tentou novamente pelo
launcher. Os logs `desktop/54364375` e `desktop/54367703` registram
`Environment in use` em `prepare-tools.ps1:14`, às 09:51:59 e 09:52:02.
Essas recusas de concorrência são distintas do erro original de linkagem.
O driver concluiu e encerrou antes da entrega; não ficou build em segundo plano.

Não houve Computer Use, execução de dashboard/jogo, teste visual ou suíte
completa. 6717 e Forza não foram recompilados nesta rodada; seus recibos
anteriores não certificam os CMake corrigidos.
