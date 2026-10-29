# Sessão gráfica Linux x64 — Iruka no WSLg, 29/10/2026

**Resultado:** o ELF AOT do Iruka abriu uma janela SDL/X11 no WSLg e renderizou
o golfinho animado, fundo do mar e textos durante uma sessão limitada a 40 s.
O backend foi **Xenos/Vulkan com Mesa llvmpipe (CPU)**. Este teste não demonstra
aceleração pela GPU física, interface principal Linux ou dashboard Linux.

## Ambiente e isolamento

- Ubuntu 26.04 LTS, WSL x86-64, `DISPLAY=:0`, WSLg/Xwayland; SDL reportou `x11`.
- Projeto `330c6f5600ff3b64f329fff1b80596dcb1437789`; artefatos da preparação
  nativa descrita no [relatório anterior](cross-platform-final-2026-10-29.md).
- SDK privado `/root/.cache/x360n-native-validation/rexglue-sdk`, HEAD verificado
  diretamente: `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com os patches já aplicados.
- Executável e bibliotecas ELF existentes, sem recompilação ou alteração de SDK.
- `Media` copiado do demo de referência para o conteúdo isolado da worktree;
  todos os arquivos copiados conferidos por SHA-256. `default.xex` preservou
  `66ec34f2f558fe4e419d926108bf458a5c634ea8b5f76771b48ba633ecb61e31`.
- User data e cache próprios em `.local/linux-tools/graphics/{user,cache}`.
  Conteúdo original, perfil/saves e artefatos Windows da pasta principal preservados.

Diagnóstico posterior do hardware: Windows identificou **NVIDIA GeForce RTX 4070**,
driver `32.0.16.1742`, status OK. No Ubuntu, `/dev/dxg`, `libdxcore.so` e
`libd3d12.so` estavam presentes. O pacote `mesa-vulkan-drivers 26.0.3-1ubuntu1`
não forneceu ICD/biblioteca `dzn` (Dozen); os ICDs instalados expuseram somente
llvmpipe ao aplicativo. Não havia overrides `VK_*`, `MESA_*` ou `GALLIUM_*`.
A presença do driver Gallium D3D12 para OpenGL não supre um driver Vulkan;
[documentação do Mesa D3D12](https://docs.mesa3d.org/drivers/d3d12.html).
A ausência do caminho Vulkan para D3D12 é um bloqueio observado; compatibilidade
do runtime com Dozen/RTX 4070 ainda não foi testada. Nenhum driver foi instalado
ou substituído durante esta validação.

## Execução e observação

O `x360n_gpu_preflight xenos vulkan` inicializou Vulkan e terminou com código 0.
O supervisor nativo executou o produto com `--gpu_plugin=xenos`,
`--gpu_backend=vulkan`, raízes de conteúdo/dados explícitas, `--rex-log` e
`--timeout 40`. A primeira tentativa foi interrompida pelo limite externo de
55 s enquanto ainda verificava os artefatos, antes de abrir a janela; a segunda
permitiu até 180 s para verificação e execução, mantendo 40 s para o título.

Run: `20261029T155409Z-iruka-linux-graphics`. O log identifica o único dispositivo
disponível como `llvmpipe (LLVM 21.1.8, 256 bits)`, API Vulkan 1.4.335,
driver `libvulkan_lvp.so`. A swapchain foi criada em 1280×720 e depois 1920×1080.
Capturas da própria janela X11, inspecionadas visualmente, mostram o golfinho em
posições diferentes e o contador do demo avançando de 4 s para 38 s.

O supervisor registrou `launched=true`, `timed_out=true`, duração **40.296 ms**
(40,296 s), zero linhas de stub, zero mensagens não implementadas, zero erros
e zero críticos. O encerramento foi provocado pelo prazo, não foi uma saída
natural nem um retorno à dashboard. Após a execução, não restavam processos
`iruka` ou `x360n` no WSL.

Avisos observados: ausência de `gamecontrollerdb.txt` e caminho diagnóstico
`ShaderDumpxe:\CompareBackEnds` sem dispositivo VFS. Os controles não foram
exercitados. O CSV registrou 1.380 amostras, mas seus contadores/FPS não foram
homologados como medição de desempenho; não se anuncia benchmark deste teste.

## Evidências locais

Base: `C:\Users\fabeg\Desktop\x360\.local\worktrees\cross-platform-final`.
Os arquivos permanecem ignorados pelo Git; nenhuma imagem/conteúdo do demo foi
adicionada à distribuição.

- `.local/evidence/runs/20261029T155409Z-iruka-linux-graphics/`: `report.json`,
  `rex.log`, `stdout.log`, `stderr.log`, `startup.csv`.
- `.local/linux-tools/graphics-preflight.{log,exit}`: preflight aprovado.
- `.local/linux-tools/graphics/media-hashes.json`: hashes das cópias de recursos.
- `.local/linux-tools/graphics/iruka-manual.png`: imagem aos 4 s.
- `.local/linux-tools/graphics/iruka-02.png`: imagem posterior; SHA-256
  `5fc07666f533bb622cf31dd94c1b9e597bb98eb34efb66a373521a4299aa69ec`.
- `.local/linux-tools/graphics/iruka-03.png`: imagem aos 38 s; SHA-256
  `bb9ca3adc7f4a5ba4af6b690d9e3c17fa4d1706eaca4781b14a34c8148e23b9b`.

Limite de metadados: `report.json.revisions.sdk_commit` capturou o HEAD do
projeto nesta worktree, cuja pasta padrão do SDK não contém o checkout privado.
Esse campo não comprova a revisão do SDK usado. A revisão acima vem da leitura
direta do checkout privado e do recibo nativo, que também identifica e verifica
os artefatos efetivamente lançados. O relatório bruto foi preservado.

## Alcance do aceite

Demonstrados: preparação AOT anterior reutilizada, verificação pelo supervisor,
carregamento do XEX do demo, janela, apresentação Vulkan por software, renderização
e animação, captura da janela e encerramento limitado no Linux x64/WSLg.

Continuam pendentes: GPU física, áudio audível, controles, frontend compartilhado,
dashboard/jogo comercial, lançamento/retorno original, saves/perfis C7/C8,
distribuição Linux e todas as demais arquiteturas. Nenhum aceite Windows foi
reexecutado ou estendido por este teste.
