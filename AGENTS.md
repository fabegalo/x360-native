# Trabalho dos agentes

Leia e siga `CLAUDE.md`, especialmente a seção **Trabalho dos agentes — regras
permanentes**, e `STATUS.md` antes de atuar neste repositório. Essas regras se
aplicam a todos os agentes: Computer Use exige autorização explícita; testes
visuais e sessões longas de jogos exigem necessidade e autorização prévia;
não repetir regressões aprovadas sem mudança relevante nem executar suítes
completas automaticamente. Priorizar testes unitários/focados e evidências
existentes. Processos em segundo plano exigem acompanhamento e duração limitada.
Interação não autorizada exige interrupção e comunicação imediata do bloqueio;
não esperar horas por aprovação. Concluir o escopo atual antes de ampliá-lo ou
começar outra funcionalidade.

## Regra arquitetural permanente — isolamento de versões

Uma dashboard usa exclusivamente seus módulos e recursos originais correspondentes.
17559 → 17559; 13604 → 13604; 6717 → 6717; o mesmo vale para versões futuras.
Inclui vk.xex, signin.xex, gamerprofile.xex, XUI, skins, fontes e demais recursos.
Nomes, ordinais e exports iguais não comprovam compatibilidade. O vk.xex 13604
é incompatível e não pode substituir o Retail 17559 já identificado.
Registrar versão/procedência e SHA-256 por perfil; verificar antes do carregamento,
sem fallback silencioso, busca por basename em outras versões ou cache global de
módulos originais. Dependência ausente ou sem identificação deve gerar erro claro.
Preservar os testes de isolamento. Infraestrutura genérica ReXGlue pode ser
compartilhada; módulos originais específicos de uma versão, não.
