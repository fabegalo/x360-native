# Native N2 — dashboard original Retail 17559

O Native D3D12 renderizou a Home original e respondeu à navegação Home → Games,
com fontes, ícones, painéis e animação originais. A execução usa shaders e dados
enviados pelo guest. Não há imagem substituta, shader pack por título, chamada
ao executor Xenos ou fallback para outro plugin.

Isso demonstra esse fluxo da 17559, não compatibilidade geral com dashboards,
Guide ou jogos. O Native continua experimental. **Vulkan ainda não está
implementado**: a interface de backend permanece independente da API; uma
solicitação Vulkan é recusada explicitamente. Xenos continua sendo o padrão.

## Implementação

| Parte | Comportamento implementado |
|---|---|
| PM4 | Shaders inline/por endereço, estados de binning, draws auto-indexados/DMA e eventos com conclusão do trabalho Native antes de publicar o resultado guest |
| Shaders | Análise guest e tradução DXBC do SDK pinado, cache próprio por conteúdo/tipo/modificação; colisões de hash conferidas contra os dwords |
| Primitivas | Triângulos, strips, fans, quads e retângulos; expansão genérica, normalização de endian dos índices, viewport/scissor e planos de clipping |
| Estados | Constantes compactadas pelo mapa do shader, samplers, interpoladores, depth/stencil, máscaras de cor, blend, gamma, exponent bias e MSAA por ROV |
| Texturas | Upload/detiling/endian de texturas 2D e mip tails, swizzle com replicação de componentes, formatos 8/16 bits, RGBA8, BC1/2/3 e formatos float utilizados; R16 inteiro convertido para float |
| EDRAM | Buffer Native de 10 MiB, escrita ordenada por ROV, kernels de clear/resolve e retorno dos bytes resolvidos à memória guest antes do scanout |
| Execução | Device/provider e presenter comuns, mas command lists, allocator, fences, buffers, descriptors e PSOs de draw/resolve pertencem ao Native |

Reutilização deliberada: parser PM4 e registradores, analisador/tradutor de
shaders, funções puras de formato/endereço/viewport e kernels de conversão do
ReXGlue/Xenia. Não são instanciados nem compilados no Native o command processor
D3D12 do Xenos, seu pipeline cache, texture cache, render-target cache ou shared
memory. A função de cálculo de formato EDRAM foi extraída com o aviso de licença
original preservado em `src/native/edram_format.cpp`.

O pin continua `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. O patch 0075 estende
o hook condicional 0074 para inspecionar o payload sem consumir o cursor.
Comprimentos/endereço de shader, DMA, registros e fontes de índices inválidas
são recusados antes do parser. Esse hook não é compilado no Xenos. São 74 patches
ativos; 0073 continua retirado. `deps.lock.json` registra os hashes e a cadeia.

## Capturas e comparação

Capturas reais do recurso de apresentação da GPU, 1280×720. O diagnóstico
`--x360n_guest_capture=DIRETORIO` é desligado normalmente, funciona com ambos os
plugins e salva PPM a cada dois segundos por no máximo trinta segundos. O worker
é encerrado antes da destruição do runtime. A conversão PPM → PNG conserva os
pixels RGB; não há correção, recorte, montagem ou inversão posterior da imagem.

Uma tentativa com PrintWindow produziu preto e não foi usada como comprovação.
A primeira Home Native tinha o eixo Y invertido; a correção foi no viewport
D3D12, antes da rasterização. As capturas de entrega são posteriores à correção.

| Cena | Native | Xenos |
|---|---|---|
| Home | [PNG](evidence/native-n2-2026-10-14/native-home.png) | [PNG](evidence/native-n2-2026-10-14/xenos-home.png) |
| Games, após RB | [PNG](evidence/native-n2-2026-10-14/native-games.png) | [PNG](evidence/native-n2-2026-10-14/xenos-games.png) |

As imagens mostram os mesmos textos, ícones e organização. A dashboard anima a
posição dos painéis e o fundo; as capturas não congelam o guest no mesmo frame.
Não se afirma igualdade pixel a pixel da cena inteira. Os caminhos e hashes das
capturas ficam no JSON junto aos PNGs. Nenhum XEX, dump de shader, fonte original
ou arquivo de perfil é incluído nos commits.

Na escala RGB de 0 a 255, o erro absoluto médio do frame inteiro foi 0,298 na
Home e 2,323 em Games; máximos 55 e 202. São diferenças observadas, não um gate
de equivalência: o segundo par também contém deslocamento de animação. Na área
fixa do botão/legenda Select (x=90..189, y=640..689), ambos os pares tiveram média
0,512 e máximo 2. A pequena área inferior direita também difere entre plugins.
Não se atribui toda diferença a arredondamento nem se certificam avatares.

Runs de entrega: Native `20261014T215155Z-n2-native-prepared-navigation`, usando
o executável preparado e seu recibo aceito pelo supervisor; Xenos
`20261014T214653Z-n2-xenos-navigation-final`, com o plugin de referência preservado.
Home foi capturada no arquivo de atraso 16 s; Games no de 30 s. Ambos receberam
RB pelo script local aos 22,3 s. Os limites de 35 s encerraram os processos,
com duração reportada de 35.234 e 35.164 ms, respectivamente.

Os runs usam os módulos/recursos originais exclusivamente da 17559, com user,
profile e cache roots de diagnóstico separados. Os saves e perfis existentes
não são usados. A seleção global persistente e seu snapshot por sessão continuam
como em N1; o argumento explícito do runner apenas seleciona a comparação.

## Validação

- Compilações incrementais D3D12 RelWithDebInfo e Release, via devshell pinado.
- `native.texture`, `native.packet-payload`, `native.abi` e
  `sdkcheck.Native-packet-guard` aprovados. O último testa pelo plugin real o
  opcode desconhecido, o shader inline truncado e o draw de índices imediatos:
  todos exigem exit 78 e a mensagem correspondente, sem atingir o marcador guest.
- O filtro de testes também executou dois testes de recibos/preparação, ambos
  aprovados; nenhuma suíte completa foi executada.
- Probe de scanout após o renderer: 48 envios, 24 capturas, 2.359.296 canais,
  erro máximo zero e shutdown limpo. Não é benchmark de FPS.
- `apply-sdk-patches.ps1 -Check`: cadeia e hashes dos 74 patches conferidos.
- A tentativa `--d3d12_debug=true` informou que a camada de debug não está
  disponível. Não conta como aprovação da camada de validação D3D12.
- Preparação oficial `scripts/build-title.ps1 -Title dash_17559`: saída 0,
  recibo publicado, marcador incompleto removido e execução final aceita.
  O driver invalidou objetos após o build de desenvolvimento não certificado;
  o codegen principal registrou 0 arquivos escritos/428 inalterados. Não se
  editou o recibo nem se contornou a validação de preparação.

Logs focados: `.local/logs/n2-native-tests.log`,
`n2-packet-payload-gpu-final.log`, `n2-scanout-final.log`,
`n2-sdk75-check.log` e `n2-final-prepare.log`.
Os runs preservam os avisos/stubs XAM e XLIVEBASE; não são anunciados como
execuções sem erros. Encerramentos por timeout são limites deliberados do teste,
não evidência de fechamento normal do guest. Há também tentativas anteriores
com exit 78, erros de compilação e uma execução interrompida sobre DLL anterior
após falha de build; estas não contam como validação da mudança.

## Limites e próximo trabalho

O caminho privilegia correção inicial: faz upload de vértices/texturas e
readback síncrono por resolve, sem cache persistente de texturas nem residência
incremental da memória guest. Não há resultado de performance equivalente ao
Xenos ou ao Pinyon Shift, nem alegação de maior eficiência. Cache/invalidação,
alocação de upload e submissão assíncrona precisam de medição posterior em
condições equivalentes, preservando os pixels e a sincronização guest.

ROV é requisito do backend D3D12 atual. Memexport, tessellation, pontos/linhas
rasterizados, primitive restart, tipos de textura além dos implementados e
queries de oclusão continuam incompletos; os caminhos reconhecidos como não
implementados falham explicitamente. MSAA segue a estratégia ROV do SDK,
inclusive quatro amostras host para o modo guest 2×. Isso não certifica todos
os modos gráficos nem todos os formatos/sinais de textura.

Não foi demonstrada transição para um jogo neste incremento. Home/Games não
certifica My Games → jogo → dashboard, Guide, avatares ou todos os efeitos da
interface. Vulkan requer seu próprio executor, recursos e tradução SPIR-V;
não deve encapsular o pipeline Xenos. C7/C8, isolamento de versões e seleção
global continuam sendo contratos da próxima etapa.

Os recibos locais existentes de Castle Crashers e Forza já registravam hashes
de `deps.lock.json` diferentes do HEAD anterior a N2. Não foram alterados nem
contornados para forçar uma transição: esses títulos precisam de preparação
oficial antes de uma nova sessão integrada. Seus saves/perfis e binários
existentes foram preservados; não houve nova certificação de gameplay.

Referências: [SDK pinado](https://github.com/rexglue/rexglue-sdk/tree/f5337cdc947ff6d4c4196737e2c807a48f2a1fc2),
[definições guest Xenos](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/xenos.h).
O código compilado usa a revisão local pinada, não o HEAD desses projetos.
