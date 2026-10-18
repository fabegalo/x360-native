# Entrada após inicialização da sessão em segundo plano — 18/10/2026

Relato: teclado e controle ocasionalmente não respondem quando a sessão começa
sem foco. Reprodução sintética autorizada pelo mantenedor, limitada a 15 segundos
por execução, sem jogos nem teclas injetadas no sistema.

## Causa e reprodução

`SessionWindow` anexa a janela SDL de cada título como filha de uma janela
persistente, em outra thread/processo. Antes da correção, chamava `SetFocus`
incondicionalmente no anexo e novamente apenas em `WM_SETFOCUS` do frame.
A ativação pertence ao frame; a SDL recebe mensagens da filha. O foco nativo
da fila pode apontar para a filha enquanto a raiz ainda está em segundo plano.
Nesse momento a SDL corretamente recusa o foco, porque a raiz não é foreground.
Ao ativar a raiz, o filho pode não receber outra transição que sincronize a SDL.

O teste novo usa duas raízes com filas distintas e uma filha SDL real, com o
mesmo protocolo de anexo. Antes da mudança, os dois primeiros ciclos após criar
uma filha apresentaram `native=1 sdl=0`; os ciclos seguintes recuperaram o foco.
Resultado preservado: 43/45 asserções aprovadas, duas falhas, exit 42. Não se
trata de uma reprodução de gameplay, mas da divergência de foco denunciada.

O efeito no controle também tem uma causa concreta: a SDL fixada em
`SDL_joystick.c`, `SDL_PrivateJoystickShouldIgnoreEvent`, descarta eventos quando
há janelas e `SDL_GetKeyboardFocus()` é nulo, com a política padrão. A correção
não habilita comandos em segundo plano nem remove esse filtro.

Referência Win32: [SetFocus](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus)
e [WM_ACTIVATE](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-activate).
Relações pai/filho entre threads podem compartilhar a fila de entrada;
[explicação da Microsoft](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143/).
Não foi acrescentado `AttachThreadInput` nem uma rotina para roubar foreground.

## Mudança

`src/supervisor/session_window.cpp` reconcilia o foco depois do anexo e das
mensagens `WM_ACTIVATE`, `WM_NCACTIVATE` e `WM_SETFOCUS`, por mensagem privada
assíncrona. Somente a raiz foreground, não minimizada, pode focar sua filha.
O supervisor encaminha a ativação atual à filha; o handler Win32 existente da
SDL valida o foco nativo e a raiz antes de atualizar seu próprio estado.
Handles destruídos ou que deixaram de pertencer à raiz são ignorados.

É uma correção compartilhada pelo supervisor de todas as sessões. Não altera
renderer, SDK, patches, PPC, XAM, módulos originais nem executáveis dos jogos.
Não acrescenta polling ou instrumentação ao runtime: o trabalho acontece nas
transições de ativação. Não foi medida performance de gameplay.

## Validação

Target explícito `x360n_session_focus_checks`, excluído de `ALL` e sem registro
automático no CTest, tag oculta `[.session-focus]`. Não executar sem autorização
para mudar o foco do desktop. Não usa conteúdo proprietário, GPU ou guest.

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Debug --target x360n_session_focus_checks -- -j4
.local/build/win-amd64/tests/Debug/x360n_session_focus_checks.exe '[.session-focus]'
scripts/devshell.cmd cmake --build .local/build/desktop-only --target x360-native -- -j4
```

- Mesmo teste de foco após a correção: 45/45 asserções, seis ciclos, duas filhas
  sucessivas, `native=1 sdl=1` desde a primeira ativação; exit 0, cerca de 2 s.
- Extensão relevante com joystick virtual interno da SDL: 89/89 asserções,
  exit 0, cerca de 6 s. Botão recebido em foreground e suprimido em background.
  Não é um dispositivo virtual instalado no Windows nem entrada enviada a apps.
- Regressão focada `[parent-window]`: 26/26 asserções, exit 0, cerca de 16 s.
  Dois processos com XEX sintético retornam pela ReXApp real; a raiz oculta
  permanece válida e reutilizável após encerrar cada filho. Sem sessão de jogo.
- Frontend Debug separado recompilado em cerca de 10 s. SHA-256 do
  `src/desktop/x360-native.exe`: `e86a5da0050e01e21a465b76b2cea59fc62f9aa52790549f0009a6c634e16ef8`.
  Supervisor copiado ao lado do frontend e original de `src/supervisor`
  idênticos: `6459a35732b10010a7f5cebdf5106ccf4b2e0f78ecdee6a6f81493be91b8f8e2`.

Logs e resultados antes/depois em
`.local/evidence/aot-ctr-dispatch-2026-10-17/focus-*.log` e `*.result.json`.
O atalho `x360-native (main)` e `scripts/start-desktop.cmd` apontam ao frontend
Debug separado. É necessário encerrar a sessão e reabrir esse launcher para
usar o supervisor novo; processos já abertos não são substituídos.

Nenhuma suíte completa ou regressão dos dez jogos foi executada. Teclado físico,
controle físico e gameplay ficam para o teste manual do mantenedor. Biblioteca,
perfis, saves e patches foram preservados; sem push ou Release.
