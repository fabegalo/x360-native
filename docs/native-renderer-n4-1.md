# N4.1 — cores 3D e contrato bindless

Investigação local em 2026-10-15, sobre `aa3277b` da branch
`wip/native-n4-gpu-resident`. O checkpoint N4 e suas otimizações de memória,
conversão GPU, caches e invalidação foram preservados. SDK v0.10.0 no commit
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com os 74 patches existentes.

## Causa isolada e correção

A corrupção aparecia no primeiro pixel de cada quad 2×2 do processamento final
da imagem. Capturas anteriores à N4 já contêm o defeito: não foi demonstrada uma
regressão introduzida pelo espelho GPU da N4. O HUD não atravessa o mesmo caminho.

O snapshot do draw final contém os registradores, microcódigo original e os
intervalos de memória lidos pelo draw. O probe o reproduz sem jogo, menus ou
interação. Compilação de pipelines Xenos obrigatoriamente síncrona nesse probe:
com compilação assíncrona, o único draw pode ser pulado e uma imagem preta não
serve como referência.

| Experimento controlado | Resultado |
|---|---|
| Conversão CPU forçada, com sincronização de bytes guest | Defeito 3D permanece |
| Conversões GPU versus CPU, incluindo LUTs volumétricas | Concordam |
| Scanout Native/Xenos do mesmo framebuffer RGB10 e gamma | Pixels idênticos |
| Inspeção dos resolves HDR anteriores ao shader final | Sem o padrão de corrupção 2×2 |
| Draw isolado Native bindful | Defeito presente |
| Mesmo draw Xenos ROV bindful, compilação síncrona | Mesmo defeito |
| Mesmo draw Xenos ROV bindless, compilação síncrona | Defeito ausente |
| Mesmo draw Native bindless | Zero canais diferentes do Xenos bindless |

O caminho de acesso bindful às texturas do shader traduzido é a diferença causal
demonstrada nesta GPU. Não se atribui o defeito a endianness, ao conteúdo da LUT,
ao resolve ou à coerência do espelho. A instrução exata do compilador/driver que
produz o resultado incorreto ainda não foi isolada; não há comprovação de um bug
específico do driver. Os DXBC Native/Xenos bindful também foram comparados e eram
idênticos.

Native passa a usar a ABI bindless do tradutor compartilhado: índices por estágio
em `b4`, heap de samplers e três arrays tipados de SRV (2D, 3D e cube) nos espaços
1, 2 e 3. A tabela usa o maior índice atribuído mais um, incluindo samplers e
aliases signed/unsigned. Os descritores mantêm a duração até a fence existente.
Exige resource binding tier 2, com erro explícito se indisponível. Nenhum efeito
foi desabilitado e a solução não contém condição por título ou hash de shader.

`native_texture_cpu` é exclusivamente diagnóstico, desligado por padrão.
`native_draw_dump` e `native_draw_dump_ps` permitem uma captura opcional de draw.
Dados proprietários capturados permanecem em `.local`, fora do commit.

## Reprodução focada

Validação final real: `20261015T230918Z-n4-bench-n41-fixed`, encerrada pelo
supervisor após 240.315 ms. Entrada manual do mantenedor, sem Computer Use.
`bench-n41-fixed/17921057608058569/frame-132.ppm` mostra o Viper amarelo e o
ambiente com cores coerentes com o Xenos
(`bench-n41-xenos/17921025657581627/frame-126.ppm`). Os instantes de câmera são
diferentes; a comparação visual não é uma alegação de igualdade de frames.

Comparativa: `.local/validation/native-n4/n41-comparison.png`, com recortes 1:1.
Originais PNG: `n41-fixed.png` e `n41-xenos-matched.png`; hashes e resultado
numérico em `n41-results.json`, gerado por `final_evidence.py` no mesmo diretório.

O FPS estável da cena **não foi obtido**. As janelas registradas durante a
progressão 3D foram 0,10–0,19 FPS, seguidas de uma pausa prolongada no cockpit.
Há pausas a frio e o bloqueio final não foi diagnosticado: não atribuir toda a
espera a compilação, oclusão ou à mudança bindless sem uma medição controlada.
Os 6,3–11,5 FPS anteriores são evidência histórica, não um par A/B equivalente.
O mantenedor recusou nova execução e pediu concluir com estas evidências.
Não há ganho de desempenho demonstrado. O último contador registrou 63.698
cargas GPU e zero cargas CPU; o caminho lento não foi usado como solução.
O supervisor registra stubs/erros de runtime e timeout; isto não certifica
jogabilidade, saída limpa do jogo nem resolução dos bloqueios existentes.

Testes finais, após build bem-sucedido:

| Teste | Resultado |
|---|---|
| GPU versus CPU, incluindo offset de scratch e texturas 3D | 4.005 asserções em um caso, passou |
| Probe scanout Native | 48 frames, 4.718.592 canais, erro máximo 1/255, shutdown limpo |
| Probe ABI | ABI 1, rejeições de contrato/Vulkan e ausência de Xenos, passou |
| Draw isolado nos dois backends | 2.764.800 canais, zero diferenças, erro máximo zero |
| Build do plugin do título e dos probes | Saída 0; sem recompilar o AOT do jogo |

Logs finais: `.local/logs/n41-final-{textures,scanout,abi,native,xenos}.log`,
`n41-final-focused-build.log` e `n41-final-game-build.log`. O build verificou os
patches do SDK; há um warning de ordem de inicialização em header upstream.

Construir somente plugin e probes, sem recompilar o AOT completo do título:

```powershell
scripts\devshell.cmd cmake --build .local\build\titles\forza_horizon --target rexgpu-native
scripts\devshell.cmd cmake --build .local\build\win-amd64 --config Release --target x360n_native_probe x360n_native_tests
$probe = '.local/build/win-amd64/src/native/Release/x360n_native_probe.exe'
& $probe native .local/validation/native-n4/final-native --draw-replay .local/validation/native-n4/isolated-draw
& $probe xenos .local/validation/native-n4/final-xenos --draw-replay .local/validation/native-n4/isolated-draw
```

`--draw-replay` atualmente suporta draw não indexado, alvo 1280×720 e saída
RGB10; não é um replay geral de frames ou de EDRAM anterior. O snapshot usado
sobrescreve o alvo inteiro. `PASS isolated draw capture` certifica a conclusão
da captura, não sua fidelidade: comparar separadamente os PNGs dos dois backends.

Evidências locais preservadas em `.local/validation/native-n4`:

- `draw-native/draw.png` e `draw-xenos-sync/draw.png`: bindful com defeito.
- `draw-native-bindless/draw.png` e `draw-xenos-bindless-sync/draw.png`: igualdade.
- `raw-trigger/resolves`: estados intermediários HDR e framebuffer final.
- `replay-native/frame-24.png` e `replay-xenos/frame-24.png`: scanout idêntico.
- `n41-cpu-complete.png`: diagnóstico CPU com a corrupção ainda presente.

## Diferenças arquiteturais verificadas no código de referência

Referências: ReXGlue/Xenos pinado (implementação derivada de Xenia), checkout
ShiftGlue `1e8968f`, e snapshot Pinyon em
`.local/reference/pinyon/n3-renderer-review`. O diretório Pinyon não é um checkout
Git; o hash do repositório pai não identifica sua revisão. A licença BSD-3-Clause
do ShiftGlue foi conferida e incluída nos avisos para os componentes derivados
presentes na N4. Nenhum executor específico de Forza foi incorporado.

| Caminho | Native atual | Referência e consequência |
|---|---|---|
| Draw / EDRAM | ROV em buffer, depth/blend no shader | ShiftGlue `PrepareTargets`/`BindTargets` usa surfaces RTV/DSV e propriedade de tiles; hardware executa depth/blend |
| Estado repetido | Reconstitui bindings por draw | ShiftGlue usa assinatura de geração, tiles, formatos e stencil para evitar repetir a preparação |
| Texturas / memória | Espelho GPU com watches, conversão compute e cache; preservados | Xenos compartilha memória e usa índices bindless; este contrato foi adotado na N4.1 |
| Resolve | Buffer EDRAM, conversão e readback pendente | ShiftGlue planeja regiões por proprietário e transfere aliases preservando representação; readback condicionado ao consumidor CPU |
| Submissão | `Flush` fecha, executa, sinaliza **e espera**, depois recicla todos os recursos | O acoplamento reciclagem/fence impede CPU e GPU de avançarem em paralelo |
| Leituras CPU | Intervalos pendentes podem provocar flush por sobreposição | Precisam distinguir dependência real, intervalo relevante e consumidor; remover a espera sem essa prova quebra coerência |
| Shaders | Tradução/criação síncrona; pausas a frio | Cache e preparação de pipelines reduzem hitches; pular draws durante compilação não serve como ganho de fidelidade |

O executor Vulkan Pinyon também contém caminhos específicos de stencil e passes
EDRAM; há opções que reduzem MSAA no snapshot estudado. Elas não são uma solução
admissível para ganhar FPS mantendo fidelidade. O valor divulgado de cerca de
120 FPS não é uma comparação equivalente com esta execução na RTX 4070:
hardware, cena, resolução, MSAA, aquecimento e temporização precisam coincidir.

Próxima etapa arquitetural: separar submissão de espera com recursos por lote
aposentados por fence e rastrear dependências reais de memória. A implementação
de oclusão `EVENT_WRITE_ZPD` exige resultados reais e continua separada desta
correção de cores. Nenhum resultado de 60/120 FPS é alegado nesta etapa.
