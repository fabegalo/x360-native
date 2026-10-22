# G1 — Vulkan/Xenos e submissões Native: incremento em andamento

Base confirmada: `main`, `3effaac5d4fb921493e3ac9044ca74ffd15840f0`.
Windows x64, LLVM 22.1.8, Release. SDK preservado em
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
O checkout já continha trabalho de perfis/Storage; esse trabalho foi preservado.
Datas de logs em UTC podem aparecer como 22/10; a execução local foi em 21/10.

**G1 não está concluída.** Os resultados abaixo são de probes sem janela, não
certificam dashboard, gameplay, Guide original ou distribuição com Vulkan.
O mantenedor autorizou sessões curtas de dashboard/jogos, mas elas ainda não
foram realizadas nesta etapa. Computer Use não foi autorizado nem utilizado.

## Auditoria e baseline

Foram lidos N3, N4.1, N4.2, arquitetura e metodologia de performance, e examinados
plugin/command processor/ScanoutBackend, D3D12, memória, texturas, pipelines,
seleção no supervisor/launcher e os backends do SDK pinado.

Constatações anteriores à mudança:

- A fábrica Xenos já aceita `GpuCreateInfo.backend`, mas ReXApp chamava
  `LoadGpuPlugin` sem encaminhar a API. O preset Windows desabilitava Vulkan.
- D1OVL só tinha composição D3D12. O caminho genérico de comandos de sistema
  já preserva/restaura estado do título e conserva registradores D1OVL.
- Native possui executor D3D12 próprio, conversão compute de texturas, espelho
  guest e cache de PSO. Seu ShaderCache produz DXBC, não SPIR-V.
- `Flush` submetia, sinalizava e esperava toda vez, reciclando imediatamente
  allocator, heaps, uploads, scratch e referências de texturas.
- Native recusa explicitamente ZPD, memexport e tessellation; o guard de pacotes
  também não cobre todos os comandos de sistema usados pelo Guide atual.
  A composição D1OVL do SDK/Xenos não implementa essa camada no Native.
- O Vulkan do SDK não equivale a um Native/Vulkan. Seus caches e command
  processor são acoplados ao executor Xenos. Reutilizá-lo como Native violaria
  a separação exigida.

N4.2 relatou 5,34–5,44 FPS em duas janelas de condução, hitches de segundos e
saída 78 em ZPD. Esses números são **históricos**, não baseline atual.

Baseline atual anterior à mudança de submissões:

- Native/D3D12: 48 frames sintéticos, 4.718.592 canais comparados, erro máximo
  de uma unidade de 8 bits, encerramento limpo. Log
  `.local/evidence/logs/g1-baseline-native2.log`, capturas em
  `.local/evidence/validation/g1/baseline-native2/`.
- Replay do draw retido da N4, com resolve/apresentação: passou. PNG SHA-256
  `038f08b74461738915a0c9c2ed8698e8a66f0c9bec9f45b6683f5324b04becda`.
  É uma reprodução restrita com tons neutros, não uma cena completa.
- Binários de controle preservados em `.local/build/g1-baseline-validated/`;
  `.local/build/g1-control/` usa o mesmo probe atualizado com o plugin anterior.
- A primeira cópia de baseline foi descartada como evidência válida: misturou
  artefatos durante o rebuild e caiu antes de produzir resultado.

## Código implementado

### Xenos/Vulkan

O preset raiz mantém D3D12 e habilita `REXGLUE_USE_VULKAN`. O configure encontrou
o glslang do SDK; sua geração de versão exige Python 3 na máquina de compilação.
Neste ambiente foi passado `PYTHON_EXECUTABLE` explicitamente com o Python
disponível. Isso não é uma dependência de execução dos binários finais.
Presets/driver de títulos e distribuição ainda precisam integrar essa política.

Patch `0126-explicit-gpu-backend.patch`: ReXApp encaminha `gpu_backend` por
RuntimeConfig até `LoadGpuPlugin`. `GpuCreateInfo` e a ABI 1 do plugin permanecem
iguais. A estrutura C++ RuntimeConfig mudou: consumidores devem ser recompilados
com o runtime correspondente, sem copiar uma DLL nova para um AOT antigo.

Patch `0127-vulkan-display-overlay.patch`: RequestDisplayTexture recebe o fetch
da superfície sem sobrescrever os registradores do título. O compositor compute
gera SPIR-V de GLSL, compõe antes de gamma e conserva alpha do título. Usa BGRA
sem byte swap, retângulo/offset, escala de display, descriptors por frame,
barriers explícitas e aposentadoria de image/view/memory por submission no resize.
Falha de composição retorna falha de apresentação, sem aceitar frame parcial.
O nome do arquivo distingue esse patch do `0127-content-enumerator-xuid.patch`
da tarefa paralela; ambos permanecem registrados separadamente no lock.

### Seleção e preflight

`graphics.toml` passa a persistir `api = 'd3d12' | 'vulkan'`. Arquivos antigos
assumem D3D12, mantendo Xenos/D3D12 como padrão. O supervisor retira overrides
de plugin/API dos argumentos dos manifests e injeta uma seleção explícita,
proveniente do snapshot da sessão, em cada transição.

O launcher separa Renderer e API, identifica Vulkan como experimental e mostra
Native/Vulkan indisponível. Selecionar uma combinação incompleta não autoriza
fallback: o supervisor recusa Native/Vulkan antes de iniciar.

`x360n_gpu_preflight.exe`, staged somente em builds Vulkan, carrega o plugin da
configuração correspondente e cria o provider/presenter real sem HWND. O
supervisor limita esse processo a 20 segundos e informa falhas de plugin, ABI,
loader, driver, recursos ou timeout. Não carrega o driver dentro do launcher.
A entrada Unicode `wmain` é necessária ao GetExecutablePath do SDK; o failfast
da primeira implementação estreita foi diagnosticado com pilha e corrigido.

Limites atuais: o preflight experimental ainda não autentica seu próprio
executável pelo recibo de preparação; a integração de recibos/empacotamento
precisa ser concluída antes de distribuir Vulkan. O preflight D3D12 legado
continua principalmente sob autoridade do loader do runtime.

### Native/D3D12

Três arenas de gravação conservam allocator, heaps de descriptors, uploads,
scratch e referências transitórias até a fence correspondente. Submissões sem
escritas visíveis à CPU podem continuar em voo. A reutilização da arena espera
sua fence; writes/readbacks, sincronizações guest e drain continuam esperando
a conclusão obrigatória. Memória guest, EDRAM e texturas persistentes continuam
ordenadas na mesma direct queue. Os caches de textura/PSO foram preservados.

Isso não elimina os readbacks de scanout, a publicação de resolves por pacote,
o custo de bindings nem a criação síncrona de PSOs novos.
As arenas podem reter até três conjuntos de uploads/descriptors/scratch;
o custo de RAM/VRAM dessa retenção ainda não foi medido em gameplay.

ZPD continua recusado. Uma query de hardware D3D12 não mede automaticamente o
depth/stencil emulado por ROV. Tampouco é correto copiar o fallback de contagens
fictícias do handler compartilhado. A implementação precisa de contadores
reais dos samples, incluindo cobertura, depth/stencil, discard e MSAA, com
publicação cumulativa no formato little-endian original. Não foi introduzido
sucesso fictício nem ignorado o comando.

## Validação atual

GPU dos probes: NVIDIA GeForce RTX 4070, driver 617.42. O log Vulkan identifica
instance API 1.4.341 e device API 1.4.351, com `VK_KHR_swapchain` habilitada.
O probe exige que a solicitação Vulkan produza o GraphicsSystem de nome Vulkan.

| Prova | Resultado | Evidência local |
|---|---|---|
| Xenos/Vulkan scanout, tiling, endian, swizzle, LUT/PWL | 48 frames / 4.718.592 canais; erro máximo 1 | `g1-xenos-vulkan-scanout.log` |
| D1OVL Vulkan, resize a cada frame, alpha/BGRA, disable, gamma | 48 frames / 4.128.768 canais; erro máximo 1 | `g1-overlay-vulkan.log` |
| Mesmo D1OVL D3D12 | 48 frames / 4.128.768 canais; erro máximo 1 | `g1-overlay-d3d12-corrected.log` |
| Capturas equivalentes D1OVL entre APIs | 5 PNGs com pixels e hashes idênticos | `overlay-api-comparison.json` |
| Configuração, migração e overrides | 34 asserções / 4 casos | `g1-renderer-tests.log` |
| Encaminhamento RuntimeConfig / ABI | 6 asserções / 1 caso | `g1-runtime-backend-test.log` |
| Snapshot da API e 80 transições sintéticas | 900 asserções / 2 casos | `g1-session-api-snapshot{,-green}.log` |
| Native ABI e Vulkan recusado sem Xenos | passou | `g1-abi-native.log` |
| Preflight Vulkan real | exit 0, 0,577 s | `g1-preflight-vulkan-wmain.log` |
| Preflight plugin ausente / Native Vulkan | exit 3, sem fallback | `g1-preflight-{missing-plugin,native-vulkan-rejected}.log` |
| Xenos/API desconhecida | fábrica recusou, probe exit 1 | `g1-xenos-unsupported-backend.log` |
| Native, 4.096 draws, resolve e captura | passou; mesmo PNG do baseline | `g1-native-stress-4096.log` |
| Pilha completa de patches a partir dos pins | 128 patches / 130 arquivos / zero divergências | `patch-replay.json` |

Logs `g1-*.log` ficam em `.local/evidence/logs/`; JSONs e capturas em
`.local/evidence/validation/g1/`. O debug layer D3D12 foi solicitado no stress,
mas não está disponível nesta máquina: **não há aceite pelo debug layer**.
Não foi executada suíte completa. Não houve push, tag ou Release.

O teste novo de snapshot teve uma falha inicial no arquivo-fixture: o stream
de texto do Windows escreveu CRLF e a comparação esperava LF. O fixture passou
a gravar em binário; as cinco asserções passaram. O caso de 80 transições já
havia passado suas 895 asserções e não foi repetido. Total dos contratos
relacionados aprovados: 940 asserções/sete casos, além dos probes de pixels.

A primeira tentativa de comparação D3D12 recusou o nome incorreto esperado pelo
probe (`D3D12`); a fábrica retorna `Direct3D 12` antes de criar o command processor.
O probe foi corrigido e a execução válida está identificada acima.

Reprodução de patches: arquivos do commit pinado e gitlinks correspondentes
foram exportados para scratch, patches aplicados na ordem do lock e todos os
hashes finais conferidos. `core.autocrlf=false` preserva os bytes LF. A tentativa
anterior com conversão CRLF divergiu por finais de linha e não foi aceita.

## Medição de submissões: cenário sintético restrito

Experimento controlado final: 4.096 repetições do mesmo draw retido, um
resolve/present, cache de PSO aquecido. Hashes iguais do probe e de rexruntime
foram exigidos antes da execução; somente rexgpu-native.dll varia. Tempo de
parede inclui inicialização e encerramento do processo. O debug layer foi
solicitado igualmente e estava indisponível nos dois lados.

| Amostra | Antes (s) | Depois (s) |
|---|---:|---:|
| 1 | 1,707 | 1,332 |
| 2 | 1,421 | 1,328 |
| 3 | 1,410 | 1,360 |

Medianas de aproximadamente 1,421 e 1,332 s (6,2% menos tempo de processo neste
microcenário). Todos os PNGs são idênticos ao baseline. Três amostras de um draw
neutro não medem gameplay, GPU time isolado ou estabilidade de frametime;
**nenhum ganho de FPS de jogo está certificado**. Identidades dos binários e
resultados: `batch-comparison-controlled.json`.

A tentativa anterior com 1.024 draws foi **invalidada**: a conferência posterior
detectou rexruntime.dll diferente entre controle e candidato. Ela não demonstra
efeito causal da mudança. Seus dados, inclusive a amostra que piorou, foram
preservados em `batch-comparison.json`:

| Amostra | Antes (s) | Depois (s) |
|---|---:|---:|
| 1 | 2,081 | 1,491 |
| 2 | 5,449 | 7,124 |
| 3 | 2,895 | 1,697 |

Não usar a mediana dessa primeira tentativa como resultado de performance.

## Matriz real e pendências

“Histórico” usa somente as evidências N3/N4/STATUS anteriores; não certifica este
build. “Probe” significa framebuffer offscreen, sem sessão original.

| Combinação | Compila/carrega | Frames/fidelidade | Dashboard/jogos | Guide/notificações | Performance |
|---|---|---|---|---|---|
| Xenos/D3D12 | sim, Release | D1OVL/scanout sintético aprovado | histórico; sem reteste atual | histórico + D1OVL sintético atual | sem benchmark atual |
| Xenos/Vulkan | sim, Release; API confirmada | scanout e D1OVL sintéticos aprovados | não verificado | D1OVL sintético; Guide original não verificado | sem benchmark de gameplay |
| Native/D3D12 | sim, Release | baseline e stress restritos aprovados | histórico parcial N3/N4; sem reteste atual | implementação atual incompleta | microcenário 6,2%; gameplay não medido |
| Native/Vulkan | não implementado | nenhum | nenhum | nenhum | não medido |

Pendências para o aceite G1: executor Native/Vulkan próprio; ZPD e predicação
corretos; composição Guide/D1OVL Native; testes de EDRAM/depth/stencil/MSAA,
resolves/shaders/blend entre APIs; Vulkan em títulos/AOT e recibos; runtime sem
Vulkan SDK na máquina final; swapchain/fullscreen/transições originais; sessões
equivalentes com controle humano para fidelidade e benchmark completo.

## Coordenação do diretório compartilhado

O mantenedor autorizou na sessão de perfis a coordenação das três tarefas.
SDK, lock, CMake e headers foram congelados para um único preparo oficial
17559 Xenos/D3D12 por Storage/perfis. A condição mínima que exclui o preflight
de builds sem Vulkan foi acordada antes de o driver retomar. Não foi iniciado
outro preparo em paralelo nem alterado o conteúdo original de qualquer versão.
Integrações adicionais ficam pendentes dessa janela estável; não se deve
declarar o produto D3D12 desse preparo como certificado para Vulkan.

Para reproduzir os probes sintéticos (Python é somente dependência de build
do glslang; fornecer o caminho de um Python 3 disponível ao configure):

```powershell
scripts\devshell.cmd cmake --preset win-amd64 -DPYTHON_EXECUTABLE=C:/caminho/python.exe
scripts\devshell.cmd cmake --build --preset win-amd64-release --target x360n_native_probe x360n_tests x360n_sdk_checks -j 4
.local/build/win-amd64/src/native/Release/x360n_native_probe.exe xenos:vulkan .local/evidence/validation/g1/repro-vulkan --overlay
.local/build/win-amd64/src/native/Release/x360n_native_probe.exe xenos:d3d12 .local/evidence/validation/g1/repro-d3d12 --overlay
.local/build/win-amd64/tests/Release/x360n_tests.exe '[renderer],[session-graphics]'
.local/build/win-amd64/tests/Release/x360n_sdk_checks.exe '[gpu-backend]'
```
