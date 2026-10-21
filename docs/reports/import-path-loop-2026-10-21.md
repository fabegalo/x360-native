# Importação de pasta presa na verificação de caminhos — 21/10/2026

## Diagnóstico

O mantenedor relatou mais de 30 minutos ao importar Forza Horizon de uma pasta
extraída. O launcher Release do atalho estava ativo desde 05:03:49; uma thread
acumulava cerca de 30 minutos de CPU. A operação `2585968` não tinha resultado
persistido, e o catálogo ainda estava vazio. Não era uma extração ISO/STFS.

`Safe()` em `src/desktop/model/imported_games.cpp` subia por `parent_path()` até
encontrar uma raiz por igualdade lexical. Quando as grafias diferiam, podia
alcançar `C:\`, cujo pai é o próprio `C:\`, e repetir indefinidamente. O atalho
executa `scripts/start-desktop.cmd`, que passa `--root` contendo `scripts\..`;
a pasta escolhida pelo seletor pode ter a grafia absoluta direta. A adoção de
conteúdo já em `.local/inputs/games` combina esses caminhos. Barras finais e
componentes `.` também reproduzem o problema.

O laço existe desde `dc98dab` (14/10); a adoção de conteúdo local foi acrescentada
por `e53acb2` (18/10). Não há medição histórica equivalente que atribua a piora
percebida a um único commit. O defeito foi confirmado independentemente do
tamanho do jogo: uma importação sintética com dois arquivos excedeu o limite
de 5 segundos e precisou ser encerrada. Não é um benchmark de throughput.

## Correção

- Interromper a subida quando o pai é o próprio caminho. A verificação canônica
  de contenção e a recusa de links em cada ancestral visitado são preservadas.
- Normalizar lexicalmente os dois caminhos antes de persistir `content_root`,
  para que a adoção por um ambiente escrito com `.`/`scripts\..` não gere um
  registro relativo inválido.
- Testar barra final, `.`, adoção local com ambiente alternativo e a forma
  `scripts/..` usada pelo atalho.

Não foram retirados hashes, alterados originais, módulos de dashboard, SDK,
perfis ou saves. Não houve execução de dashboard/jogo nem Computer Use.

## Validação e limites

Build Release pelo `scripts/devshell.cmd`, árvore `.local/build/desktop-only`.
`x360n_desktop_tests` e `x360-native` foram ligados com a correção; o executável
apontado pelo atalho foi atualizado. O processo antigo preso foi encerrado
somente após identificar PID e caminho exatos.

- `[universal],[extraction]`: 23 casos / 390 asserções aprovados, incluindo as
  três primeiras variações de caminhos.
- Após acrescentar a variação exata do atalho, `[import-path-termination]`:
  16 asserções aprovadas nas quatro variações. Cada variação levou
  aproximadamente 0,11–0,16 s com arquivos sintéticos.
- Logs em `.local/tmp/import-check-20261021/`: `baseline.txt`, `regressions.txt`
  e `final-paths.txt`. O timeout anterior não estabelece uma razão de ganho.
- As primeiras tentativas de testes no TEMP restrito tiveram erros de acesso;
  os testes sintéticos foram repetidos dentro da pasta gravável do repositório.
- A tentativa real usou o teste `[.universal-real]`, com raiz `scripts\..` e
  fonte `.local/inputs/games/forza_horizon/disc-1` (2.432 arquivos,
  7.320.461.527 bytes), sem AOT nem execução de jogo. Ela superou a etapa que
  podia entrar em loop, mas recusou a escrita por trava ocupada. A repetição
  fora do sandbox também recusou a trava: não era apenas restrição do sandbox.
- Restart Manager identificou dois proprietários de `environment.lock`:
  PowerShell executando `scripts/build-title.ps1 -Title dash_17559` e outro
  executando `scripts/build-title.ps1 -Title forza_horizon`. Esses trabalhos
  não foram interrompidos; a trava não foi apagada nem contornada.

A importação real ainda não foi concluída/certificada. É necessário repeti-la
quando os builds liberarem o ambiente. Não foi medido o tempo integral de
importação real corrigida, nem há novo aceite funcional do Forza.
