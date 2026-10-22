# C8 — armazenamento de saves por perfil

Implementado a partir de `f9d6c54`, em Windows x64. Patch reproduzível `0067`,
SDK mantido em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
Implementação e testes no commit local `d95fe15`.

## Contratos implementados

- `XamContentCreate` / `XamContentCreateEx`: criar novo, abrir existente,
  abrir/criar, substituir e truncar. O XUID vem da identidade conectada no
  `UserProfile` do C7; o Title ID vem do cabeçalho de execução carregado por
  `KernelState`. Slot diferente de zero ou desconectado falha para saves.
- `XamContentCreateEnumerator` / `XamEnumerate`: catálogo do perfil/título
  atual, sem incluir saves do diretório compartilhado. Metadados antigos não
  podem redirecionar uma entrada para outro proprietário. Erro ao acessar o
  diretório não se transforma em catálogo vazio.
- `XamContentGetCreator`, thumbnails e `XamContentDelete`: usam a mesma
  identidade; exclusão de pacote aberto falha. Nomes de pacote inválidos são
  rejeitados antes de tocar no host.
- `NtCreateFile`, `NtReadFile`, `NtWriteFile`, `NtClose`: continuam usando
  `XFile` e `HostPathDevice`, com arquivos reais do host sob o mount do pacote.
- `NtFlushBuffersFile` e `XamContentFlush`: propagam a operação de flush até
  `FlushFileBuffers` no Windows / `fsync` no backend POSIX, verificando o retorno.
  Fechar conteúdo também verifica o flush dos arquivos ainda abertos.
- Conclusão overlapped de Create/Ex mantém a fila existente e publica resultado,
  disposição e extended error reais. O nome do mount é copiado antes de enfileirar.

Um contador de sessão invalida mounts e enumeradores anteriores após sign-out
ou troca de identidade. Voltar ao mesmo perfil não reativa handles antigos.
Arquivos mantêm referência ao dispositivo: fechar/remontar um pacote não deixa
ponteiros de entradas pendurados em handles sobreviventes. A referência é liberada
com o último arquivo; não se acumulam cópias de pacotes nem mounts aposentados.

Falhas não são testadas com `XSUCCEEDED` onde o contrato é Win32: códigos
positivos de erro não significam sucesso. Falha de exclusão não vira abertura
bem-sucedida; falha de escrita do header chega ao chamador. Um alias ocupado,
inclusive `game:`, é rejeitado antes de qualquer substituição destrutiva.

## Organização e preservação

```text
<user_data_root>/
  profiles/session-v1.dat                   # perfil selecionado e estado (0138)
  <XUID de 16 dígitos hex>/
    FFFE07D1/00010000/<XUID>/                # pacote do perfil (0138)
    FFFE07D1/Headers/00010000/<XUID>.header
    <Title ID de 8 dígitos hex>/
      00000001/<nome do pacote>/<arquivos do jogo>
      Headers/00000001/<nome do pacote>.header
  0000000000000000/...                       # conteúdo compartilhado existente
  cache/...                                 # dados internos existentes
```

Layout existente preservado; nenhum save foi movido ou atribuído automaticamente
a outro perfil. Saves legados sem proprietário ficam intactos, mas não são
expostos como saves de um usuário conectado. Conteúdo instalado continua em seu
tipo/diretório original. O patch não muda módulos, fontes ou recursos de dashboards.
Configurações de perfil específicas de título (`XamUser*ProfileSettings`) mantêm
o armazenamento anterior do SDK; são um serviço separado de pacotes XContent.

## Evidência executada

Os testes invocam os exports PPC dentro de threads do runtime, com memória guest,
XEX sintético com Title ID real no cabeçalho e persistência real no host. Não
substituem as operações de disco por mocks e não chamam diretamente um backend
de saves alternativo.

- `Save storage uses XAM and NT file contracts across profiles and restarts`:
  Player1 grava/lê 27 bytes; Player2 não os vê e grava outros 27 bytes no mesmo
  nome de pacote; Player1 recupera seu conteúdo. Handles e enumeradores antigos
  falham após trocar. Outro Title ID tem catálogo independente.
- `Save storage reports real failures and completes overlapped operations`:
  arquivo bloqueando diretório, dispositivo inválido, nomes inválidos, usuário
  ausente, arquivo/pacote ausente, alias ocupado, exclusão de pacote aberto,
  truncamento real, falha de header sem publicar mount, preservação de save
  legado e `CreateEx` assíncrono.
- Três casos finais `[save-storage],[aggregate-content]`: **486 assertions**.
  Onze casos relacionados `[local-profile],[aggregate-content]`: **331 assertions**,
  preservando C7 e enumeração/lançamento de conteúdo instalado. Há sobreposição
  do caso aggregate, portanto **13 casos distintos**, não 14.
- `tests/local/save-storage-process.ps1` executa duas instâncias independentes:
  uma grava, encerra; a outra recupera ambos os payloads e exclui apenas o de
  Player2. Grava PIDs, hashes dos binários e snapshots SHA-256 dos arquivos.
  CTest `sdkcheck.save-storage-processes`: passou, 1,83 s;
  evidência final: `.local/evidence/runs/c8-storage-c6cd3b7d92684173818c894e2996f84b/report.json`.
- Replay dos 67 patches desde os fontes pinados, verificando hashes de entrada
  e finais: `.local/evidence/c8/replay-acceptance.log`. Builds e logs focados em `.local/evidence/c8/`.

Reprodução focada após compilar `x360n_sdk_runtime_checks` pelo devshell:

```powershell
scripts/devshell.cmd ctest --preset win-amd64-debug -R 'sdkcheck.Save storage' --output-on-failure
scripts/devshell.cmd ctest --preset win-amd64-debug -R sdkcheck.save-storage-processes --output-on-failure
```

## Limites da comprovação

Castle Crashers foi recompilado e executado por scripts locais em três sessões
limitadas (aproximadamente 39, 54 e 54 segundos), com armazenamento isolado,
logs e screenshots em `.local/evidence/runs/c8-castle-{1,2,3}`. Chegou ao menu original
em modo trial. Não houve pacote de save produzido; o script não confirmou foco
na janela. **Save/load de progresso comercial e o ciclo completo dashboard →
jogo → save → dashboard não foram comprovados.** Não se atribui essa ausência
ao modo trial sem evidência adicional.

Comprovado: Windows x64, um usuário conectado no slot 0, dois perfis alternados,
pacotes extraídos e operações pelo caminho XAM → VFS → host. Não comprovados:
POSIX, múltiplos usuários simultâneos, tolerância a queda de energia, STFS novo
assinado/criptografado, Xbox Live ou compatibilidade universal. A descoberta do
HDD e seus números de capacidade eram os valores virtuais do SDK nessa etapa.
O incremento de dispositivos de 21/10 substitui esses números por backing real,
mantém o layout C8 e acrescenta MU/USB configuradas, quotas e consulta NT do
volume do pacote. O seletor original 17559 foi exibido em teste curto autorizado,
com conclusão de seleção e cancelamento; houve interação concorrente, e escolha
da MU e uso por jogo ainda precisam de aceite. Implementação e limites estão no
[relatório S1](reports/storage-p0-2026-10-21.md). APIs de instalação/abertura
direta de containers fora desse fluxo não foram ampliadas.

A etapa C8 histórica acima não usou Computer Use. O incremento S1 teve teste
visual explicitamente autorizado; não houve suíte completa, push ou Release.

## HDD único da sessão (22/10)

Regra de fidelidade ao console: todos os títulos de uma sessão gravam XContent
(saves, DLC, instalações e configurações de perfil por título) no **HDD da
dashboard da sessão** (`--content_data_root`, patch 0136). Cada título mantém
sua própria raiz para cache e dados internos. Não há migração: por decisão do
mantenedor, saves antigos nas raízes dos títulos deixam de ser usados (não são
apagados). Sessões de outra dashboard usam o próprio HDD, como consoles
distintos. Jogos precisam ser recompilados com o SDK do 0136 para adotar o HDD
único; um runtime anterior ignora a flag e continua usando a própria raiz.
