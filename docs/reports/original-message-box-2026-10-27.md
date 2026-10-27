# Caixa de mensagem original — 27/10/2026

## Pedido e ponto de partida

O usuário pediu commit/push das correções genéricas anteriores e, depois, trocar
a caixa de `XamShowMessageBoxUI` pela interface original, sem ImGui ou ajustes
por jogo. O snapshot anterior foi publicado em `origin/main` no commit
`fb995474544a9f83c5a52219857e74cba617ca5a`. O servidor aceitou o push direto com
as permissões da conexão; não houve force-push nem alteração de regras locais.
Os testes desta correção e o commit final também foram expressamente pedidos.

Computer Use continua autorizado somente em Forza Horizon 2 e Minecraft, com
console/perfis/saves isolados e até três minutos por tentativa. O usuário não
estará presente; a validação não pode depender de input físico manual.

## Originais e contrato observado

Selecionados exclusivamente os originais 17559 já identificados no perfil:

- `system/xam_ui.xex`: SHA-256
  `78b3dbd8ae866f4258f5444205486ea9f8451afaa30f469c875dcf345f828467`.
  Imagem carregada: `235c5f6aa71a98ec7ec94d7e825198bf97c38099af9bb174ad2ab0642b182d07`.
- `system/hud.xex`: SHA-256
  `37e10612afde211e1d349838704f7cc2648ad5a4d1f8ca459f18ecd2c1f31ac7`.
- Skin, fontes e demais dependências pertencem ao mesmo perfil e continuam
  passando pela verificação existente; sem outro dashboard ou busca por basename.

Análise estática: export 02CA → `816C5508`; ele valida parâmetros e monta
1.148 bytes. Envia `FE/2100D`; o dispatcher original em `816C7964` seleciona
`hud.xex`, controlador em +4 e tipo de aplicação 1 em +8. Não existe um novo
desenho a reconstruir: o HUD original cria a caixa pelo seu XUI/compositor.

| Campo | Offset / capacidade original |
|---|---|
| Cabeçalho, controlador, tipo | +0, +4, +8 |
| Título UTF16 | +12, 43 unidades incluindo terminador |
| Texto UTF16 | +98, 384 unidades incluindo terminador |
| Quantidade de botões | +868 |
| Até três rótulos UTF16 | +872, passo 86 bytes, 43 unidades cada |
| Botão inicial, flags | +1132, +1136 |
| Resultado, OVERLAPPED | +1140, +1144 |

`816C16C8` conserva o HRESULT do serviço em last-error e mapeia E_OUTOFMEMORY
para 14, demais falhas para 1627. A API assíncrona recebe seu nono argumento
na área de saída PPC +0x54 e retorna 997; quem decide e conclui é a aplicação
original, sem escolher o botão inicial em nome do usuário.

## Implementação e verificações iniciais

0213 transporta o pedido original com strings copiadas e limitadas, valida
memória guest antes de ler/escrever e encaminha ao host da versão selecionada.
O caminho `XamShowMessageBoxUI` deixa de criar ImGui e de aceitar defaults no
modo headless. Serviço ausente/incompatível produz erro; não há fallback visual.
O host 17559 reutiliza a carga, cenas, input, empilhamento e compositor do HUD
existentes, com descriptor próprio para `2100D` e tamanho 1.148.

Teste sintético anterior: **1/6 verificações falhou**, pois o pedido não chegava
ao serviço. Após o patch: **217 verificações/2 casos**; limites UTF16, três
botões, argumentos nulos/inválidos, buffers não mapeados, flags, modo síncrono
sem saída, modo assíncrono, conclusão antecipada e falhas do host.
Controles relacionados: **688/22** de UI/seletor/contexto e **185/4** de posse
dos botões. Um filtro de input inicialmente não correspondia a nenhuma tag;
os controles de input acima foram executados separadamente com as tags reais.

A primeira compilação apontou duas referências a XThread com include faltante.
Nenhum teste foi executado sobre esse build falho. O include foi incorporado
ao staging, com reversão/reaplicação pelos scripts oficiais e hashes atualizados.
Replay de 0213: **200 patches/271 arquivos, zero divergências**, 60,6 s,
`patch-replay-2cd1fee736a14f569161c74dba7b6f63.json` na evidência local.

Uma revisão do fechamento encontrou `XMsgCompleteIORequest` reassociando o I/O
ao worker que fecha a UI. Teste com duas threads guest reproduziu **1/13
verificações falhando**: o handle solicitado F8000010 foi substituído por
F8000014. 0214 preserva o contexto registrado na submissão e registra esse
contexto também no seletor. A falha de um pedido modal enfileirado preserva
igualmente seu solicitante. Controles finais, incluindo duas threads e o dono
registrado antes do despacho do seletor: **903 verificações/28 casos aprovados**.
A preparação Minecraft iniciada com 0213 foi
interrompida somente em sua árvore identificada; marcador incompleto preservado
até concluir novamente o fluxo oficial. O novo preparo oficial foi concluído
antes da validação visual, com o conjunto final de 201 patches.

O diagnóstico explícito de serviço está desligado normalmente. Quando pedido,
emite uma única mensagem sintética pela API PPC, em qualquer jogo da sessão,
para testar visual, input e retorno sem depender de mineração/controle físico.
Não injeta input, não seleciona botão, não altera licenças nem acrescenta clock
ou wrapper ao frame normal quando desabilitado. O supervisor limita a tentativa
e usa somente cópias de console; a mensagem natural do Minecraft usa a mesma API.

Evidências CPU locais em `.local/evidence/original-message-box-2026-10-27/`.

Replay final após 0214: **201 patches/271 arquivos, zero divergências**, 61,4 s,
`patch-replay-a3e70981ead445789d4da35086ad52f9.json`. Minecraft concluiu o preparo
oficial `e6f28333e57b4928803fc4df003f5263`: 14/146/39/12/168 s, retorno zero
em todas as etapas, geração zero escritos/269 inalterados e marcador incompleto
removido oficialmente. Host/serviços 17559 também concluíram preparo oficial,
`8234c5898833414eb70fd4e0408dfe59`: 13/147/33/13/271 s e cinco geradores de UI,
retorno zero em todas as etapas. Geração principal: zero escritos/428 inalterados.

## Validação visual e retorno da API

Computer Use foi realizado pela skill `computer-use`, exclusivamente nas janelas
dos executáveis genéricos de Minecraft e Forza Horizon 2. No Minecraft foram duas tentativas de 120 segundos,
com a mesma cópia isolada do console e a configuração global Xenos D3D12,
sem override de renderer, licença ou identidade do jogo. O diagnóstico chamou
a API pública uma vez, com dois rótulos e o botão inicial 1; nenhum input foi
injetado pelo runtime. Os comandos de controle vieram do Computer Use autorizado.

O HUD original exibiu a caixa grande branca/cinza, ícone, fontes, destaque verde,
botões verticais e indicações A Selecionar/B Voltar. A mensagem sintética continha
o título “Mensagem de sistema” e texto em português com acentos. É a aplicação
original 17559 renderizada pelo compositor existente, sem desenho ImGui.

| Tentativa | Input e conclusão observada | Encerramento |
|---|---|---|
| `minecraft-original-box-b-9` | B: result `0000065B` (1627), extended `000004C7` (1223), selected `FFFFFFFF`, user 0; foco liberado | 120.166 ms, limite, sem crash |
| `minecraft-original-box-a-10` | A no botão “Cancelar” destacado: result/extended 0, selected 1, user 0; foco liberado e retorno visual à tela do Minecraft | 120.163 ms, limite, sem crash |

O botão A confirma a seleção do rótulo; escolher um botão chamado “Cancelar”
retorna sucesso com índice 1, enquanto B cancela o pedido com 1627/1223. Os
valores são produzidos pelo original e foram preservados sem normalização.
O toque Shift+Up e depois Up não mudou o destaque nesta tentativa; não há aceite
da navegação para outro botão, nem evidência que atribua isso a uma falha do HUD.
Não foi alterado o mapeamento de input para contornar a observação.

Logs rotacionados `rex.2.log` em cada diretório `run`:

- B: criação/submissão 13:10:19.436; fechamento 13:10:31.923 após 743 frames;
  conclusão 13:10:31.938.
- A: criação/submissão 13:13:45.435–436; fechamento 13:14:24.562 após 2.341 frames;
  conclusão 13:14:24.576.

Ambos os preflights autenticaram 7.287 arquivos/2.574.551.269 bytes; executável
SHA-256 `7769349e8831fde4f253bfce4b0c7aaeeaedee4bc2e0e5c68b17d2ff7aeede32`.
37/37 arquivos do console de origem preservados em cada tentativa. O supervisor
encerrou a árvore no limite; o exit code 1 do filho encerrado não foi classificado
como crash ou saída normal do jogo. Evidências em
`.local/evidence/generic-games-2026-10-26/{minecraft-original-box-b-9,minecraft-original-box-a-10}/`.

Esta etapa demonstra abertura, desenho, seleção por A, cancelamento por B e
retorno da caixa original pela API compartilhada. Não certifica outras APIs
de UI, outras dashboards, Native, licença comercial, save/reload ou campanha.
O gameplay inicial dos dois jogos continua documentado separadamente no build
0212; não foi repetido para ampliar o escopo desta correção de UI.

O Forza concluiu também o preparo oficial com 0214,
`8c828ad16e954093a2549463784461a4`: etapas 13/154/103/12/338 s, todas com retorno
zero. Código gerado: zero escritos/643 inalterados. O preparo fez rebuild seguro
por não encontrar estado incremental autenticado; publicou novo recibo e retirou
seu marcador incompleto. Minecraft, Forza e host 17559 ficaram preparados com
o mesmo lock final, preservando os módulos de cada versão.

Controle visual adicional `fh2-original-box-16`: a mesma requisição genérica
exibiu a caixa original sobre a abertura do Forza. B fechou o HUD e voltou
visualmente a “Pressione Start”. Log `rex.log`: criação 13:29:36.871, submissão
997 em 13:29:36.872, fechamento após 1.498 frames em 13:30:02.341, conclusão
13:30:02.355 com result 1627/extended 1223/selected `FFFFFFFF`/user 0.
O toque W (analógico esquerdo mapeado) também não mudou o destaque nesta
tentativa curta; navegação continua sem aceite. Nenhuma configuração foi alterada.

Executável Forza SHA-256
`68f82b66fdcb231537858da6711d2809ad6fef6d3ef9dbde8693b8637d9ade0d`;
preflight de 12.275 arquivos/18.109.272.751 bytes. Encerramento no limite de
90 segundos (90.215 ms), sem crash capturado, 37/37 arquivos de origem
preservados. Não houve novo teste de condução. Evidência local em
`.local/evidence/generic-games-2026-10-26/fh2-original-box-16/`.

## Verificações de entrega

O lote CPU focado foi executado sobre o binário Release recém-compilado:

```powershell
& .local/build/win-amd64/tests/Release/x360n_sdk_runtime_checks.exe '[message-box-request],[message-box-io-context],[profile-ui],[signin],[storage-selector],[guide-context],[guide-opening],[guide-input]'
```

Resultado: **903 verificações/28 casos**, em
`.local/evidence/original-message-box-2026-10-27/checks-final-test.log`.
Não foi executada suíte completa nem repetida a regressão depois de aprovado
o mesmo código. O supervisor diagnóstico também foi recompilado antes do uso.

O gate `.github/scripts/check_contributions.py`, com base em `fb99547` e título
Conventional Commit, aprovou texto/metadados, formulários e links locais.
Inicialmente faltava o parser YAML do Python utilizado; instalado PyYAML 6.0.2
somente em `.local/tools/contribution-checks`, sem modificar pacotes globais.
O gitlink e o HEAD do SDK continuam em
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; mudanças do SDK são exclusivamente
os patches registrados, aplicados pelo script oficial e conferidos pelo replay.
