# Conquista com som e sem imagem — camada de vídeo original

## Evidência do reteste

Sessão do mantenedor: `20261021T125515Z-session-3-forza_horizon`.
Às 10:01:56.685, a fila original aceitou o aviso. `notify.xur`, `notify.xus`,
`Achievement.png` e `NotifyPopup.xma` foram carregados sem erro. O mantenedor
relatou novamente ausência do visual. Às 10:02:06 o Guide foi aberto e compôs
seu frame normalmente. O log inicial identifica DXGI/NVIDIA RTX 4070; o produto
Windows usa D3D12. A correção anterior da inicialização não bastou.

## Causa e implementação

O caminho original sem HUD (`816CE060` → `816CB3D0`) renderiza a cena da
notificação numa textura e programa a camada de vídeo do console pela rotina
`81840768`. Os comandos escrevem `D1OVL_CONTROL1`, superfície, pitch, offsets,
início/fim e update; `818406C0` controla enable. Esses registradores existiam
na tabela do SDK, mas não eram consumidos pela apresentação do backend.
Além disso, a restauração de registradores após o command buffer de sistema
apagava o estado de scanout junto com o estado de desenho do jogo.

Patch 0114, sobre o mesmo SDK pinado:

- Preserva os registradores D1OVL após executar comandos de sistema; os demais
  registradores de desenho, shaders e estado do título continuam restaurados.
- Decodifica os formatos RGB de 32 bits usados pelo driver original, endereço,
  pitch e retângulo; rejeita dados inválidos e formatos ainda não suportados.
- D3D12 consulta a textura no cache existente sem sobrescrever fetch constants
  do jogo. Compõe os pixels da superfície original antes de gamma e FXAA.
- Pipeline criado no primeiro uso; textura intermediária reutilizada, com
  liberação diferida ao mudar de resolução. Sem camada habilitada, não há
  alocação, consulta de textura nem dispatch adicional.
- Descritor de sistema vazio desliga a camada no swap seguinte. Isso cobre
  o caso em que o host substitui seus últimos comandos por um descritor vazio
  antes de o callback retornar. O comando de desligamento é emitido uma vez.
- Log único de inicialização `Guest D1OVL scanout enabled` permite distinguir
  apresentação gráfica de sucesso apenas na fila no próximo teste real.

O shader não contém nomes, pontos, imagens de conquistas ou lógica por jogo.
Esses dados e a animação continuam produzidos pelos recursos originais 17559.
Não foram alterados jogos, saves, perfis, módulos originais nem a Retail 1888.
Escopo de apresentação nesta rodada: D3D12. Vulkan e Native não são certificados.

## Validação técnica

Evidências: `.local/evidence/validation/notification-overlay-20261021/`.

- Release: 416 asserções em oito casos passaram, cobrindo decodificação,
  superfície inválida, desligamento por descritor nulo/vazio, caminho do frame
  de sistema, UI de conquistas e contexto de tarefas.
- Shader HLSL compilado com otimização e warnings tratados como erros.
  O header DXBC foi regenerado com igualdade de SHA-256 (2.020 bytes de shader).
  Compilador local: `D3DCompiler_47.dll` 10.0.26100.9711, SHA-256
  `0f89c19165fd83a05e39bea601a06007e6a8814d55e2adf913902926231f843e`.
- GPU sem janela: 48 pixels conferidos por readback, com mesclagem alpha,
  posição, escala do display, preservação do fundo e retângulo vazio. Usa
  superfícies sintéticas, sem executar jogo, dashboard, entrypoint ou perfis.
- Reaplicados os 113 patches em árvore temporária a partir do pin do SDK e dos
  pins aninhados; os 124 hashes finais coincidiram. Normalização LF da árvore
  temporária foi necessária devido ao autocrlf do Git no Windows; só aceita
  quando o resultado coincide exatamente com o hash registrado.
- Houve falhas intermediárias no harness: falta do objeto `RegisterFile` no
  link e contador estático não reinicializado entre sections. Corrigidas antes
  da execução final aprovada. Não foram considerados sucesso de produto.
- Builds Release da 17559 e do Forza concluídos pelo fluxo normal de
  `build-title.ps1`, ambos com saída zero. Recibos finais conferidos por SHA-256:
  12 artefatos da dashboard e seis do jogo, sem marcadores de preparação
  incompleta. Logs `build-dash.log`, `build-forza.log`, `products-check.log` e
  hashes em `final-products.json`, na pasta de evidências acima. Uma primeira
  conferência recusou corretamente o Forza enquanto seu recibo ainda estava
  sendo finalizado; a conferência final ocorreu após o término do processo.

Não há medição de ganho de performance: esta é implementação de uma etapa
gráfica antes ausente. Durante a notificação há uma superfície intermediária
RGBA16F e um dispatch adicional; impacto em gameplay ainda precisa ser medido.
Os testes técnicos não certificam pixels da cena original em gameplay.

## Reteste: aviso visível, mas cores e transparência incorretas

O mantenedor enviou captura de gameplay com “10G — Bem-Vindo Ao Horizon”.
A cena original apareceu, mas envolta em ciano opaco e com tonalidade vermelha.
O log da sessão `20261021T140228Z-session-1-forza_horizon`, às 11:09:15.187,
confirmou scanout da superfície `0A56C000`, retângulo `256,576–845,666`.

A análise do criador de textura original, sem modificá-lo, identificou formato
`0x18280106`: formato de texel 6, tiled, endian zero e swizzle BGRA. A rotina
`81838CB0` transfere esses campos ao fetch da textura; a rotina `81840768`
programa sua superfície no display. O fetch sintetizado pelo 0114 aplicava
`k8in32` e RGBA, confundindo ordem dos comandos PPC com a dos pixels. Um texel
branco transparente virava ciano opaco; alpha e vermelho trocavam de função.

O patch 0115 usa `kNone` e BGRA no decodificador compartilhado. Não mascara ciano,
não inventa alpha e não modifica o shader, os recursos originais ou perfis.
Também não acrescenta passes, alocações ou instrumentação por frame ao 0114.
Não foi feita medição de performance em gameplay nesta rodada.

Evidências desta correção:
`.local/evidence/validation/notification-color-20261021/`.

- Build Release dos testes concluído; 422 asserções em oito casos passaram.
- O diagnóstico GPU passou com 64 pixels: superfície BGRA de oito bits,
  branco transparente, cores primárias distintas, cinza, alpha parcial,
  posicionamento, escala, recorte vazio e preservação do jogo.
- O mesmo diagnóstico, com `--legacy-fetch`, voltou a falhar no primeiro
  texel branco transparente (canal verde), como esperado. O shader é idêntico
  nos dois casos. Isso cobre a falha que o teste anterior com floats RGBA
  não detectava. O diagnóstico exercita o decoder e a composição, sem guest;
  a transferência endian de palavras usa `xenos::GpuSwap`, sem executar o
  caminho completo de resolve/cache do jogo.
- Builds Release da 17559 e do Forza concluídos pelo fluxo normal, ambos com
  saída zero; recibos novos e os 18 hashes de artefatos conferidos, sem marcador
  incompleto. Evidências: `build-dash.log`, `build-forza.log`,
  `products-check.log` e `final-products.json` na pasta desta correção.
- Reaplicados os 114 patches em árvore isolada a partir do mesmo SDK pinado
  e pins aninhados: 124 hashes finais idênticos (`reproduction.log`).
- Captura fornecida pelo mantenedor preservada apenas na evidência local como
  `user-before.png`, com SHA-256 separado. Nenhum conteúdo original foi
  acrescentado ao repositório; nenhum jogo foi iniciado pelo agente.

## Aceite pendente

Entrar no Forza pelo launcher com o perfil local e obter uma conquista real
ainda bloqueada. Confirmar visual e som originais, desaparecimento do aviso,
Gamerscore no Guide e persistência após reiniciar. Conquistas já gravadas não
devem ser apagadas nem desbloqueadas novamente para forçar um aviso.
Nenhuma sessão de jogo ou controle de janela foi executado nesta rodada.
O mantenedor solicitou corrigir cor/transparência e fazer commit nesta rodada.
O commit não representa aceite visual final. Push não solicitado.
