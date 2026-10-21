# START curto no controle — 21/10/2026

O mantenedor relatou precisar apertar START duas vezes dentro do jogo. A janela
de reconhecimento BACK+START da 17559 também se aplica ao host compartilhado
carregado pelo jogo. Na consulta de estado, um DOWN ficava oculto por até 150 ms;
se o botão era liberado antes disso, UP apagava a espera e o jogo não recebia
nenhum estado pressionado. Isso reproduz um mecanismo compatível com o relato;
a confirmação física no jogo continua necessária.

O patch reproduzível `0112-guide-state-short-taps.patch`, sobre o mesmo SDK
pinado, preserva esse toque real para uma consulta de estado e entrega a
liberação na seguinte. Eventos de teclado do controle mantêm sua fila própria.
Gestos consumidos pelo Guide descartam o toque pendente. O estado continua
separado por usuário e é limpo pelo reset existente da conexão. Quando o filtro
muda os botões sem mudança do pacote físico, o pacote visível também avança;
isso permite que consumidores que ignoram pacotes repetidos vejam a transição.
Sem dependência de Title ID, alteração de controles, perfis ou saves.

## Evidências técnicas

- Teste novo antes do patch: falha `0 == 16` ao esperar START depois de soltar
  um toque curto; 10/11 asserções passaram, 1 falhou.
- Após o patch, Release: 659 asserções em 14 casos de toques, janela do Guide,
  reconexão e propriedade de entrada passaram. O filtro adicional
  `[system-ui-input]` não correspondia a nenhum caso; não foi contado.
- Regressão complementar com os nomes corretos `[guide-opening],[guide-input]`:
  185 asserções em 4 casos passaram.
- Cadeia dos 111 patches e hashes finais verificados por
  `scripts/setup/apply-sdk-patches.ps1 -Check` (saída 0).
- Logs em `.local/evidence/validation/launcher-fullscreen-20261021/`, prefixo
  `start-`. O polling acrescenta apenas estado por controlador e operações
  constantes, sem alocação/I/O. Não foi medido ganho de desempenho e não se
  afirma ganho. Nenhum jogo/dashboard foi executado pelo agente nesta rodada.

## Aceite

Launcher, Retail 17559 e Forza foram recompilados em Release. Os builds da
17559 e do Forza terminaram com saída 0 e publicaram recibos novos pelo fluxo
normal `scripts/build-title.ps1`; hashes em `start-final-products.json` na pasta
de evidências acima. Não houve cópia manual de DLL para contornar preparação.

Testar um toque curto de START dentro do
jogo, a liberação e outro toque, e abrir/fechar o Guide com o gesto usual.
Os testes sintéticos não certificam o controle físico nem resolvem as quedas
por acesso inválido relatadas separadamente. Sem commit/push até aprovação.
