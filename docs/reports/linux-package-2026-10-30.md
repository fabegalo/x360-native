# Pacote Linux x64 e gate de licenças/conteúdo — 2026-10-30

Primeiro pacote de distribuição Linux do launcher unificado, gerado por
`scripts/packaging/package-desktop.ps1 -Platform linux-x64` a partir do commit
`adb71e7`. Candidato local, **não publicado**.

## Como é gerado

- Mesmo script do Windows: exporta o commit, clona o SDK pinado
  (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`), aplica e confere os patches do
  lock, gera o payload `app/workspace` com `payload.sha256`.
- Compilação em WSL por `scripts/packaging/build-desktop-linux.sh`: fonte e build
  irmãos no ext4, LLVM 22.1.8 Linux e sysroot privado (Ubuntu 26.04, glibc 2.43,
  GCC 15.2.0-16ubuntu1), `-march=x86-64-v2`, Release, `X360_DISTRIBUTION=ON`,
  `-static-libstdc++ -static-libgcc`. Sem pacotes globais nem `LD_LIBRARY_PATH`
  do usuário.
- Testes da árvore isolada: `desktop.catalog` e `desktop.update-recovery` PASS.
- Dependências ELF lidas com `llvm-readobj`: apenas `libc.so.6`, `libm.so.6`,
  `ld-linux-x86-64.so.2`; `libx360n-mspack.so` (LGPL, substituível) só no
  extrator. Nenhuma versão `GLIBCXX`/`CXXABI`/`GCC` exigida.
- `.tar.gz` determinístico (ordem por nome, mtime fixo, dono 0/0, `gzip -n`),
  modos 0755 nos 4 executáveis e 0644 no restante.

## Resultado

| Item | Evidência |
|---|---|
| Arquivo | `x360-native-1.12.1-linux-x64.tar.gz`, 13.657.904 bytes, SHA-256 `cf13e5fc8bedc389183a75da58911808f932de40b17061eb7bb58cc8ac63de11` |
| Gate de licenças/conteúdo | PASS: 96 componentes, 4 executáveis linux-x64, notices originais e identidade do payload |
| Piso de glibc medido | **2.43**: `acosf`/`asinf`/`atan2f@GLIBC_2.43` no launcher (libm do sysroot); `__isoc23_*@GLIBC_2.38` em todos |
| Uso como usuário (WSL Ubuntu 26.04, glibc 2.43, sem sysroot) | SHA-256 publicado confere; `package.sha256` confere; modos preservados; `ldd` sem ausentes; `app/x360n --help` exit 0; launcher abre com SDL_GPU Vulkan |
| Extrator empacotado, Castle Crashers XBLA real | `x360n-extract extract`: 708 arquivos, 143.818.078 bytes, 0 erros, 10 s; SHA-256 do original igual antes/depois |

Importação pelo launcher do pacote não foi automatizada: as opções de teste
(`--import`, `--auto-log`) são excluídas do build de distribuição por desenho.
A mesma importação do Castle Crashers pelo launcher de desenvolvimento passou
no Windows (38 s) e no Linux (17 s), com registro de catálogo
`5126950ab93e695bdfc3a2bf59aa42d12e08b62794bb9a024b3151711fbc2b51` nos dois.
Nenhuma ISO de Xbox 360 foi encontrada nos discos locais (só ISOs de PSP), por
isso o caminho ISO não foi exercitado com conteúdo real.

## Piso glibc 2.35: sysroot Ubuntu 22.04

O candidato `1.12.1` herdava a glibc 2.43 do sysroot de validação (Ubuntu
26.04). O pacote passou a ligar contra um sysroot de destino próprio, como o
build `linux-amd64` do SDK upstream (container `ubuntu:22.04`):

- `scripts/packaging/prepare-linux-sysroot.sh` baixa, num estado apt privado e
  sem instalar nada no host, exatamente os 68 `.deb` de
  `scripts/packaging/linux-sysroot-jammy.lock` (arquivo oficial: `jammy`,
  `jammy-updates`, `jammy-security`; `main`/`universe`; 16,7 MB), confere nome,
  versão e SHA-256 de cada um, extrai e reescreve links absolutos para dentro
  do sysroot. Índices apt: 31 MB, descartados depois.
- Runtime C++: GCC 12.3.0-1ubuntu1~22.04.3 (`jammy-updates`). O upstream usa o
  GCC 13 do PPA `ubuntu-toolchain-r/test` por causa de `<format>`; o escopo
  desktop não usa `<format>` e compilou com o libstdc++ 12 sem mudança de código.
  glibc 2.35-0ubuntu3.15 e cabeçalhos X11/Wayland/GL do jammy.
- CMake, Ninja, pkg-config e `wayland-scanner` continuam vindo do toolchain de
  validação no host; o build recusa um sysroot cujo lock difere do exportado e
  grava o hash dele em `package.json` (`sysroot.lock_sha256`).
- `package-desktop.ps1` falha se o piso medido passar de 2.35.
- Notices de runtime trocados pelos `copyright` do jammy (`gcc-12-base`,
  `libc6`); o arquivo da glibc 2.35 não traz o texto da exceção dos objetos de
  inicialização, que continua nas fontes originais da glibc.

| Item | Evidência |
|---|---|
| Arquivo | `x360-native-1.13.0-linux-x64.tar.gz` (commit `59cc807`), 13.504.933 bytes, SHA-256 `c3da9ca06c820ef5d26efba48a693b943eaca484d1e6e2f879ebe2ca3acbcc8a` |
| Testes da árvore isolada | `desktop.catalog`, `desktop.update-recovery` PASS |
| Piso medido (`llvm-readobj --version-info`) | `GLIBC_2.35` em `x360-native`, `x360n`, `x360n-extract`; `GLIBC_2.2.5` em `libx360n-mspack.so`; NEEDED só `libc`, `libm`, `ld-linux` (+ mspack no extrator) |
| Gate | PASS, 96 componentes, 4 executáveis linux-x64; 7 negativos Linux rejeitados; Windows (1.10.1 com o inventário novo) 1 positivo + 24 negativos PASS |
| Uso no host (Ubuntu 26.04, sem sysroot) | SHA-256, índice e modos conferem; `ldd` sem ausentes; `x360n --help` exit 0; launcher abre com SDL_GPU Vulkan |
| Execução sobre glibc 2.35 real (loader e `libc.so.6` do sysroot jammy, “GNU C Library (Ubuntu GLIBC 2.35-0ubuntu3.15)”) | `x360n --help` exit 0; `x360n-extract extract` no Castle Crashers: 708 arquivos, 143.818.078 bytes, 0 erros, 10 s, original igual; launcher abre (renderer software: o sysroot não tem carregador Vulkan) |

A execução com o loader do sysroot usa o kernel e a sessão gráfica do WSL;
não substitui um sistema Ubuntu 22.04 ou Debian 12 limpo.

## Gate por plataforma

`licenses/inventory.json` agora separa `platforms.windows-x64` e
`platforms.linux-x64` (executáveis, unidades SDK/terceiros/geradas, bibliotecas
ligadas, entradas transitivas do SDK, componentes de runtime e dependências
vendorizadas). O escopo Windows é o mesmo de antes. O Linux acrescenta:

- unidades POSIX do SDK, com os cabeçalhos originais em `rexglue-attributions.txt`;
- fontes Linux/X11/Wayland do SDL3 e 25 protocolos Wayland gerados;
- `aes_128` (MIT) e TinySHA1 (ISC), compilados só no extrator Linux;
- `gcc-runtime` (GPL-3.0-or-later WITH GCC-exception-3.1, libstdc++/libgcc
  estáticos) e `glibc` (LGPL-2.1-or-later, do sistema, não distribuída), com os
  arquivos `copyright` originais do Ubuntu em `licenses/`.

Os depfiles de link do Linux listam objetos `.o`; eles não são lidos como
entradas do SDK.

Verificação do gate: candidato Windows 1.10.1 com o inventário novo,
`tests/packaging/licenses.ps1` com 1 positivo + 24 negativos PASS; Linux com 7
negativos focados (`.so` estranho, ELF renomeado, executável ausente, link
estático, header fora do escopo, notice de runtime ausente, plataforma
desconhecida), todos rejeitados com a mensagem esperada.

## Limites

- Piso glibc reduzido a 2.35 no `1.13.0` (seção acima); ainda sem execução num
  sistema 22.04/Debian 12 completo nem com Vulkan sobre a glibc antiga.
- O empacotamento Linux exige host Windows com WSL. Um runner Linux nativo
  (PowerShell 7 + o mesmo script de build) ainda não foi feito.
- Linux limpo fora do ambiente de build, sessão da dashboard no Linux e
  atualização real por `.tar.gz` publicada continuam pendentes. macOS e ARM64
  não foram feitos.
