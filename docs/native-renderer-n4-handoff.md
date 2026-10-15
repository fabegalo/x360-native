# Native N4 — handoff (checkpoint experimental, 2026-10-15)

Branch local `wip/native-n4-gpu-resident`, sobre `c43d679`. **Não aprovado**:
regressão de cor na cena 3D. Não mesclar na `main`. Sem push ou Release.

## O que mudou

- `src/native/guest_memory_mirror.{h,cpp}` (novo): espelho residente da memória
  física guest (`memory_`, 512 MiB) com validade por página, watches de escrita
  da CPU (callbacks de invalidação do `Memory`), `RequestRange` com caminho
  rápido sem lock, `RangeWrittenByGpu`, `CopyReadbackToGuest` só em páginas
  ainda válidas e `InvalidateCpuPages` por quadro (`clear_memory_page_state`).
  Adaptado do `SharedMemory` pinado (Xenia, BSD) e de ShiftGlue `1e8968f7`
  (BSD, Tom Clay). **Pendente:** registrar em `THIRD_PARTY_NOTICES.md`.
- `src/native/texture_load.{h,cpp}` (novo): plano de carga na GPU com os shaders
  `texture_load_{8,16,32,64,128}bpb` e `depth_{unorm,float}` do SDK (espelha
  `LoadTextureDataFromResidentMemoryImpl`), conversão CPU movida para
  `ConvertTextureCpu`. RGBA4, RGB10A2 signed e inteiro 16 bits seguem na CPU.
- `d3d12_textures.cpp`: cache com invalidação por watch (sem snapshots/épocas),
  carga GPU a partir do espelho, tabelas gravadas antes do estado do draw.
- `d3d12_draw.cpp`: vértices via `RequestRange`; pré-sync só para índices e
  texturas convertidas na CPU. `d3d12_resolve.cpp`: `RequestRange` +
  `RangeWrittenByGpu`; writeback para a CPU continua na fence.
- Sincronização: `VGT_EVENT_INITIATOR` não drena mais; drenam
  `EVENT_WRITE_SHD/EXT`, `INTERRUPT`, `REG_TO_MEM`, `WAIT_REG_MEM`,
  `COND_WRITE`, `MEM_WRITE`. `IM_LOAD`/`LOAD_ALU_CONSTANT` só sincronizam o
  intervalo lido (`packet_guard.cpp`, `command_processor.*`, `scanout_backend.h`).
- `texture.h`: endian por tamanho de bloco como os shaders pinados (8 bpb sem
  swap; 16 bpb só `8in16`) e depth unorm `(d + (d >> 23)) * 2^-24`.
- `native_stats` (desligado por padrão): frame time P50/P95/P99/1% low, causas
  de flush, opcodes e locais de overlap. `scripts/native-stats-window.ps1`.
- Testes: `tests/native/texture_load_test.cpp` (GPU×CPU byte a byte, 13
  formatos, 4 endians, linear/tiled, mips empacotados, cubo, array, volume);
  `packet_test.cpp` atualizado. Último resultado: 19 casos, 366.000 asserções.

## Reprodução e evidências

- Build: `scripts\devshell.cmd cmake --build .local\build\titles\forza_horizon --target rexgpu-native`.
- Rodada: `.local/validation/native-n4/bench-forza.ps1 -Label <x> -Seconds 150`
  (executável diagnóstico `native-n3-diagnostic`, sem recibo, rota por tempo
  `30:Return,45:Return,55:Space`). Resumo: `scripts/native-stats-window.ps1`.
- Runs: `20261015T011953Z-n4-bench-b0`, `...012410Z-...-b1-events`,
  `...012731Z-...-b1-opcodes`, `...013058Z-...-b2-range`, `...014455Z-...-b3-mirror`,
  `...014902Z-...-b4-sites`, `...020000Z-...-b5-current` (`.local/runs/`);
  capturas em `.local/validation/native-n4/bench-*`.

## Medições (cenas NÃO equivalentes)

| | B0 (HEAD + contadores) | B5 (checkpoint) |
|---|---|---|
| FPS cena 3D | 2,6–2,8 | 6,3–11,5 |
| P50 / P95 | ~365 / 384–450 ms | 84–150 / 101–217 ms |
| Esperas GPU/quadro | ~93 (todas por evento) | ~55 overlap + ~5 |
| Texturas convertidas na CPU/quadro | ~88 (52 MB) | 0 |

B0 estava parado na cena jogável; B5, com o jogo mais rápido, a rota por tempo
caiu na direção de abertura (109 mph). Indica ganho, não é comparação válida.

## Problemas abertos

1. **Regressão de cor 3D** (B3/B5): cena azul com bordas vermelhas, metades
   superior/inferior diferentes (tiles); HUD/2D corretos. Carga GPU ≡ CPU no
   teste, então suspeitas: dados do espelho (coerência resolve/upload) ou a
   nova regra de endian 8/16 bpb. Próximo passo: cvar para forçar conversão CPU
   e outro para a regra antiga de XOR, e bisseccionar.
2. **~55 falsas esperas/quadro**: o pré-sync em `d3d12_draw.cpp` inclui bindings
   com sinal não usado (variantes signed/unsigned do tradutor); usar o mesmo
   critério de `BindTextures` (sinal usado e dimensão compatível).
3. **`EVENT_WRITE_ZPD` (0x5B), exit 78** ~131–145 s: consulta de oclusão não
   implementada. SDK pinado finge 1000 amostras; Pinyon usa query de hardware
   (render targets reais). Native usa ROV: precisa contagem própria correta.
4. **Congelamentos intermitentes** na cutscene (herdado da N3.1, não revisto).
5. Depois: submissão assíncrona com quadros em voo (o Flush ainda espera a
   fence), scanout direto do espelho, índices na GPU, PSO sem chave por string,
   readback buffer que cresce sem flush, `THIRD_PARTY_NOTICES.md`, validar
   Castle/dashboard, rota de benchmark robusta a velocidade.
