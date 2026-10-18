# Charlie Murder: bloqueio no mapeamento NetCF — 18/10/2026

Base: `main` local em `6de07e6`. **Encerramento por bloqueio técnico preciso;
sem correção do runtime ou novo aceite funcional.** O contrato de páginas de
imagem do SDK é incompatível com o consumidor no RuntimeHost. A incompatibilidade
foi reproduzida com memória sintética, sem executar o jogo ou suas DLLs guest.
Os logs históricos não contêm o HRESULT interno; não se apresenta uma dedução
estática como captura dinâmica desse retorno.

Não houve Computer Use, sessão de jogo, alteração dos originais, SDK, lock ou
patches, push ou Release. Storage → perfis continua fora deste escopo.
Diagnósticos privados: `.local/evidence/charlie-netcf-2026-10-18/`.

## Sequência observada e entrypoints

As sessões `20261018T102725Z-session-1-…` e
`20261018T105409Z-session-1-…` apresentam a mesma sequência. Na segunda:

1. Thread `57740`: carrega HostLoader, registra 1.461 funções; carrega
   RuntimeHost, registra 19.633 funções.
2. A consulta por `RuntimeHost.xex` sem dispositivo gera um erro VFS, mas
   `KernelState::GetModule` também procura os módulos já carregados por
   `XModule::Matches`. A execução continua em outra thread (`24148`).
3. Essa thread chama `KeCreateUserMode`, carrega NetCF e registra 2.066 funções.
   Há duas chamadas a `KeFlushUserModeCurrentTb`; NetCF é descarregado.
4. Thread `57740`: `XamLoaderLaunchTitle(<dashboard>, 0x100)`. O processo termina
   com saída 0, sem exceção capturada, em 6.515 ms. O primeiro log registra
   1.431 ms; condições diferentes, sem conclusão de performance.

Portanto é um caminho de retorno do bootstrap, não evidência de crash XNA ou
de Guide/Xbox Home. Os registros `report.json`, `rex.log` e `handoff.bin`
anteriores foram preservados.

Os headers originais foram lidos como dados; os endereços também constam nos
registros AOT. A sonda nativa existente conferiu novamente layout, hash,
propriedade do ponteiro do entrypoint e registros dos três módulos, sem chamar
nenhuma função PPC:

| Imagem | Base / tamanho | Entry point | Registro |
|---|---|---|---|
| default.xex | `82000000 / 00120000` | `8204B478` | Presente no init AOT |
| HostLoader.dll | `82120000 / 00110000` | `82163C60` | 1.461 entradas |
| RuntimeHost.xex | `82230000 / 008F0000` | `82376AE0` | 19.633 entradas |
| NetCFUserMode.dll | `82BA0000 / 000F0000` | `82BD3A88` | 2.066 entradas |

As três DLLs têm flags XEX `0x9`. Os SHA-256 dos originais são os da preparação
anterior; a conferência completa dos recibos abaixo inclui seus bytes e as
DLLs nativas publicadas. Não foi usado outro RuntimeHost/NetCF ou módulo de
outra dashboard.

## Inicialização, imports, chamadas indiretas e transição

Referências AOT abaixo pertencem exclusivamente aos arquivos locais gerados de
Charlie em `.local/build/aot/g0a4c6253b34297dc/generated/`; não são endereços
genéricos para hooks de runtime.

- HostLoader `sub_82161E20` procura/carrega a imagem por
  `sub_8216C9E8` (`XexLoadImage`, flags 9), resolve procedimentos por
  `sub_8216C978` e chama os ponteiros com `bctrl`. Não há falha de chamada não
  registrada nos logs. Isso não certifica todos os callbacks futuros.
- `KernelState::LoadUserModule` registra a tabela nativa antes de executar o
  entrypoint da DLL com `{hmodule, 1, 0}`. Uma recusa de attach provocaria
  mensagem explícita e rollback; essa mensagem não aparece.
- O entrypoint NetCF `82BD3A88` começa com retorno 1 e trata motivos **4–7**.
  Motivo 1 retorna sem inicializar seu corpo principal. Motivo 5 chama
  `sub_82BD3A00`, depois `sub_82BC0C00` com motivo 1. Logo, o attach convencional
  do carregador não demonstra inicialização efetiva da CLR.
- RuntimeHost `sub_82389F88` chama `sub_8238D568` → `sub_8238DD60`.
  Este chama `KeCreateUserMode(0, …)`, verifica retorno negativo e segue para
  `sub_8238A708`. O stub atual preserva r3=0, de modo que essa verificação
  **não detecta a ausência da implementação**. Não é um sucesso implementado.
- `sub_8238A708` chama `sub_8238A3C8`, que carrega NetCF por
  `sub_823DD5E0` (`XexLoadImage`, flags 9). O SDK recebe essas flags, mas não
  implementa sua semântica de modo de usuário. O consumidor lê `image_base`,
  `full_image_size` e `entry_point` nos offsets `0x1C`, `0x38` e `0x3C` do
  `X_LDR_DATA_TABLE_ENTRY`; os offsets e valores do carregador coincidem.
- Em `8238A604`, o consumidor chama **`sub_8238C620(base, size)`**. Retorno
  negativo salta a `8238A69C`, descarrega a imagem por `sub_823DD538` e devolve
  o erro. Somente depois de mapear com sucesso há a possibilidade de chamar o
  entrypoint com motivo 5 via `sub_823999E8` → `sub_82396C30` →
  `KeEnterUserMode`. Esse import não aparece no log.

O SDK ainda tem `KeEnterUserMode`/`KeLeaveUserMode` como stubs. NetCF também
contém instruções `sc` emitidas como `REX_UNIMPLEMENTED`; os logs não registram
que foram executadas. Corrigir só a resolução de imports/ponteiros, mudar o
motivo do attach ou retornar sucesso dessas APIs não implementaria a transição
que o original espera. Nenhum módulo MXF/XNA posterior foi registrado nessa
execução; não há marcador conclusivo de execução gerenciada.

## Incompatibilidade determinística de PTE

`KeGetImagePageTableEntry_entry` (`src/kernel/xboxkrnl/xboxkrnl_memory.cpp` do
SDK) calcula, para o heap de páginas de 64 KiB:

```
(address - heap_base) / page_size & 0x400FFFFF
```

Não consulta alocação/proteção ou descritores XEX; os bits baixos variam com o
endereço. O patch 0005 modifica somente o heap de 4 KiB e documenta que esse
valor é uma aproximação. Seus testes anteriores não certificam o consumidor
de modo de usuário de NetCF.

O mapper `sub_8238C620` agrupa páginas pelos bits `1`, `2` e `4` e exige
combinações específicas. Um PTE não nulo com bits 0 e 1 limpos é recusado em
`8238C76C` → `8238C7A8`; o retorno é **`0x80000000`**, seguido da liberação das
páginas já mapeadas. Há também um ramo que recusa bit 2 sem bit 1 quando bit 0
está ligado. A sonda registra somente esse teste de atributos; não simula os
demais requisitos, o mapeamento ou os callbacks.

| Página NetCF | Tipo no descritor original | PTE real do SDK | Teste de atributos |
|---|---|---|---|
| `82BA0000` | Read-only data (3) | `000002BA` | Passa; outros requisitos continuam |
| `82BB0000` | Read-only data (3) | `000002BB` | Passa; outros requisitos continuam |
| `82BC0000` | Code (1) | `000002BC` | **Recusado** |
| `82BD0000` | Code (1) | `000002BD` | **Recusado** |

Assim, mesmo que as etapas anteriores tenham sucesso, o mapper não consegue
atravessar a primeira página de código NetCF. É um bloqueio comprovado e
compatível com unload/retorno antes de `KeEnterUserMode`. As duas chamadas de
flush também são compatíveis com mapeamento parcial e cleanup; **a contagem
não prova quais ramos foram percorridos**. Os logs não distinguem uma eventual
falha anterior dentro do mesmo mapper. O HRESULT dinâmico da sessão permanece
não capturado, e não se afirma que `0x80000000` foi lido daquela execução.

Não foi adicionado patch: falta validar a codificação de atributos/frame dos
PTEs, sua relação com `MmGetPhysicalAddress` e as tabelas passadas a
`KeCreateUserMode`, além da troca de contexto/callbacks de `KeEnterUserMode` e
do despacho `sc` no caminho AOT. Alterar máscaras até NetCF passar seria uma
implementação especulativa, com risco para C7/C8 e para o heap de 4 KiB já
validado. Nenhum original ou stub foi alterado para contornar esse bloqueio.

## Verificação focada e preservação

Nova ferramenta explícita `x360n_user_mode_page_probe`, fora de ALL e CTest:

```powershell
scripts/devshell.cmd cmake --build --preset win-amd64-debug --target x360n_user_mode_page_probe
.local/build/win-amd64/tests/Debug/x360n_user_mode_page_probe.exe .local/tmp/page-probe 82BA0000 15
```

A ferramenta usa somente memória sintética, sem carregar XEX ou executar
funções guest. Saída **78 significa contrato não suportado**, não teste verde.
Os resultados desta etapa:

- Build específico da sonda aprovado; gate dos 91 patches aprovado.
- 15 páginas em `82BA0000`: seis recusas no teste de atributos, primeira em
  `82BC0000`. Proteção RO → RW mantém o PTE nas 15 páginas; saída 78.
- Intervalo genérico `82100000`, oito páginas: três recusas e oito PTEs
  inalterados após RO → RW; saída 78. O defeito independe dos bytes do jogo.
  O build final repetiu o intervalo de 15 páginas com saída 78 e recusou um
  endereço fora do heap/com risco de overflow com saída 2.
- Registro real de HostLoader/RuntimeHost/NetCF aprovado sem execução guest;
  hashes das listas de endereços preservados nos logs `registration-*.log`.
- Recibos completos de Charlie, 17559, Motocross e Castle aprovados: identidade,
  inputs/artefatos/produtor, DLLs, globs, CMake e checkpoint. Não houve rebuild,
  codegen ou reemissão de recibo de título.
- Os 2.019 arquivos originais do inventário anterior conservaram seus SHA-256.
  Isso preserva a evidência dos registros, perfis e saves; não refaz seu aceite
  pela interface. A lista My Games aprovada manualmente continua sendo evidência
  histórica de `6de07e6`, não um novo teste visual.

| Produto | Inputs / artefatos | SHA-256 do recibo, sem alteração |
|---|---|---|
| Charlie | 4.013 / 35 | `774ad0418479647d96fda91bdcbdeb869fff4d753b2283617ca1179f03374278` |
| 17559 | 4.013 / 13 | `0b6ca96d328977dbb50901d8569bd1a4559c1a529bcafe75db4b6eff530d6915` |
| Motocross | 4.170 / 6 | `78d9e7c2f52111d777e5f0d61a6420f4d5b03583dfd0fca21d20757512f8c8dc` |
| Castle | 3.520 / 5 | `b6b5693e5a4db831e8296fea94df101c713615e5e11286c19680e4f0935761ac` |

Limite de build preservado: a tentativa conjunta da sonda e
`x360n_sdk_runtime_checks` parou de produzir progresso após o link do probe de
shutdown. Foi interrompida; a tentativa isolada do runner também foi
interrompida após staging sem conclusão. Nenhum teste desse runner foi executado
com base nesses builds. O build separado da **nova sonda** terminou com saída 0
antes de suas duas execuções. Logs em `build.log`, `runtime-checks-build.log` e
`probe-build.log`/`probe-final-build.log`. Sem suíte completa ou regressões de jogos.

## Próximo limite e procedimento manual

O limite funcional continua no bootstrap: CLR/XNA, renderização, entrada,
gameplay, save/load e Guide → Xbox Home não aprovados. Esta etapa entrega
reprodução técnica de um contrato incompatível; **não corrige o encerramento**.

Para fechar o motivo dinâmico exato numa futura sessão autorizada, preparar
antes um diagnóstico opt-in que capture o retorno de `sub_8238C620` em
`8238A610`, a primeira página recusada e o resultado devolvido por
`sub_8238A3C8`. Capturar também se `KeEnterUserMode` e o entrypoint NetCF com
motivo 5 são alcançados. A instrumentação deve ficar desligada normalmente e
qualquer alteração de runtime exige novos builds/recibos pelo driver oficial.
Esses pontos ainda não foram instrumentados nesta etapa.

O mantenedor pode então usar o roteiro já existente
`.local/evidence/charlie-functional-2026-10-18/run-manual.ps1`, após conferir os
recibos e a cópia isolada. Ele inicia somente a dashboard e limita cada processo
a 120 s; não executá-lo automaticamente. Navegar manualmente Home → My Games →
único Charlie, anotar última imagem/áudio e retorno, encerrar na primeira falha
e guardar logs/handoffs/diagnóstico. Não entrar em Storage → perfis. Uma repetição
sem a captura adicional só repetiria o limite já demonstrado.
