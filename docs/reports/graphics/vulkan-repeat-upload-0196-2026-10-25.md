# Uploads Vulkan repetidos — patch 0196

Base: `main` local/remota `b3bec63b725282e0019c9b24c2fab4aee13a2e7b`.
SDK preservado em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
Referência: [Xenia Canary c8f214e2a9b0209ca8a0ad6cb3dfb155fe545ae2](https://github.com/xenia-canary/xenia-canary/commit/c8f214e2a9b0209ca8a0ad6cb3dfb155fe545ae2).

`VulkanTextureCache::LoadTextureDataFromResidentMemoryImpl` agora enfileira uma
dependência `TRANSFER_WRITE → TRANSFER_WRITE` quando a imagem já está em
`kTransferDestination`. Ambos os stages são `TRANSFER`, o layout permanece
`TRANSFER_DST_OPTIMAL`, as famílias de filas são `VK_QUEUE_FAMILY_IGNORED` e
`skip_if_equal=false` impede descartar a dependência como operação vazia.
`SubmitBarriers(true)` continua antes da cópia seguinte.

A mudança no SDK tem dez linhas adicionadas, exclusivamente no cache Vulkan.
Não altera fences, filas, submissões, resolução, FPS, vblank ou o renderer Native.
O patch 0156 continua controlando os uploads de **shared memory**, distintos das
cópias para a imagem aqui corrigidas. O stream hoisted e seu replay permanecem
intactos. O worker de submissão ordenada do 0157 não foi alterado; sua interação
foi revisada no código, sem novo teste de concorrência do worker nesta etapa.

## Regressão executável e limites

`generate_vulkan_upload_probe.py` extrai no build, sem reescrever seus corpos:

- O trecho final de upload, desde `MarkAsUsed` até a criação das regiões de cópia.
- `GetTextureUsageMasks`, `PushImageMemoryBarrier`, `SubmitBarriers`,
  `SplitPendingBarrier` e `EndRenderPass`.
- A implementação completa de `DeferredCommandBuffer`, incluindo o replay hoisted.

O fixture fornece estado mínimo e uma tabela de funções Vulkan com callbacks de
captura. Construtores, alocação de recursos e contabilidade de lifetime não são
exercitados. O restante do método de upload (conversão/untile) não é executado.
Isso testa código de produção compilado e os comandos emitidos, sem depender de
comparação textual para decidir se a barreira existe ou está ordenada.

Dois uploads da mesma imagem são gravados na mesma submissão aberta. A captura
exige transição inicial, primeira cópia, dependência write/write e segunda cópia,
com imagem, masks, layouts, queue families e subrecursos corretos. Também cobre
a transição sampled → transfer, a elisão padrão de barreiras iguais e comandos
hoisted independentes antes do stream principal.

## Resultados reais

| Validação | Resultado |
|---|---|
| CPU sem 0196 | Falhou: 2 falhas / 7 verificações; barreira repetida e ordenação com hoisting ausentes |
| CPU com 0196 | 15 verificações, zero falhas |
| Hazards de upload do 0156 | 16 verificações, zero falhas |
| CTest focado `-R sdkcheck.Vulkan-` em Release | 5/5 aprovados, 5,61 s |
| Build Release `rexgpu-xenos` e probes de upload | Saída 0; warnings existentes, incluindo 196 de inicializadores no cache Vulkan |
| `apply-sdk-patches.ps1 -Check` | Saída 0; SDK pinado e hashes conferidos |
| Replay isolado na ordem do lock | 183 patches aplicados; 172 hashes finais idênticos |
| Vulkan validation layers / execução em GPU | Não executadas |
| Jogos / gameplay / fidelidade visual / performance | Não executados nem aprovados |

Os outros três casos do CTest focado são verificações existentes de fetch
predicado, lock de registro de draws e ajuste de expoente de textura. Não houve
suíte completa. Os builds passaram pelo `scripts/devshell.cmd`, LLVM 22.1.8.
Uma primeira compilação do fixture falhou pela inclusão de RenderDoc ausente;
o include foi corrigido antes da execução do teste negativo.

O replay preservou as bases Git e os submódulos pinados. O checkout isolado
precisou usar `core.autocrlf=false`, como o SDK; o hash histórico de base SDL
registra CRLF, mas seus patches requerem o blob LF. As primeiras tentativas com
conversão CRLF foram diagnósticos falhos, não validações aprovadas. Nenhum hash
histórico de base ou patch antigo foi alterado. O warning de whitespace do
patch 0114 foi preservado.

Evidências locais: `.local/patch-work/0196-red-test.log`,
`0196-release-build.log`, `0196-release-final-build.log`, `0196-final-cmake.log`,
`0196-tests.log`, `0196-sdk-check.log` e `0196-replay-final.log`.

Pendente: execução autorizada em Vulkan com validation layers/synchronization
validation, uploads repetidos reais e avaliação visual. Não há ganho de
performance demonstrado. Sem jogos, Computer Use, push, tags ou Release.
