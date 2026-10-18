# InkBoy — decisões do projeto

Este documento registra decisões curtas e estáveis. O mapa de módulos e o
fluxo atual do runtime estão em [`architecture.md`](architecture.md).

## Produto e escopo

- InkBoy é uma aplicação nativa C independente, não um frontend nem um fork de
  Gambatte-K2.
- O alvo é o Kindle Paperwhite 5 (11ª geração), firmware 5.19.2, com ARM
  32-bit hard-float.
- O protótipo atual integra gameplay via Libretro, touchscreen e profiling.
  Gameplay, controles e saída com restauração foram validados no PW5 na
  TASK-009; Power não é uma saída controlada suportada.
- Menus e configuração de runtime continuam fora do escopo atual.

## Linguagem e dependências

- C continua sendo a linguagem principal por gerar um binário nativo pequeno,
  previsível e compatível com o toolchain KindleHF.
- FBInk é a biblioteca de display: encapsula framebuffer, desenho, refresh,
  waveform e espera de conclusão através de sua API pública.
- O build usa uma versão mínima e estaticamente vinculada do FBInk, vendorizada
  no commit upstream `886f25f`.
- Docker, Make e o SDK KindleHF preservam a compilação cruzada e o fluxo de
  deployment existentes.

## Runtime e hardware

- A aplicação não acessa `/dev/fb0` nem emite ioctls específicos do Kindle
  diretamente; usa a API pública do FBInk.
- O fluxo salva o framebuffer anterior e restaura o estado em uma saída normal.
  A apresentação inicial, os redesenhos completos, as limpezas e a restauração
  aguardam a conclusão do refresh; atualizações regionais comuns submetem o
  refresh e adiam essa espera até o próximo redesenho completo, limpeza ou saída.
- O deployment fica em `/mnt/us/inkboy`; o root filesystem do Kindle não é
  alterado.
- A arquitetura Stage 2 permanece single-threaded e sem loop de animação.
- O runtime M5 é single-threaded. O padrão atual é 2 FPS com atualizações
  regionais GC16 e uma limpeza AUTO com flash a cada 60 segundos. Essa política
  foi escolhida depois do estudo TASK-010, embora os perfis de 2 FPS não tenham
  passado os critérios de aceitação medidos naquele estudo; a matriz física
  completa para o padrão atual continua pendente. A espera das atualizações
  regionais comuns é adiada até antes de uma limpeza ou atualização completa.

## Assets e licenças

- O projeto usa GNU GPL-3.0-only. Avisos e licenças de terceiros devem ser
  mantidos e a compatibilidade verificada antes de reutilizar código ou assets.

## Decisões de pesquisa atuais

- `WFM_AUTO` continua sendo usado nas limpezas e restauração. Smooth scaling
  foi avaliado no probe de pesquisa removido; o runtime de gameplay usa
  nearest-neighbor. A negociação de 48 kHz também continua experimental. O
  comportamento de atualização no PW5 segue sendo medido.
- A negociação de 48 kHz permanece apenas uma opção experimental: melhora o
  custo de emulação, mas apresentou uma regressão repetível no tempo de refresh
  e na quantidade de atualizações do display.
- O adaptador Libretro vendorizado do SameBoy possui uma alteração específica
  do InkBoy: quando não há áudio no produto, chama
  `GB_set_sample_rate(..., 0)`. A variante é identificada como `SameBoy
  1.0.5-inkboy`. A mudança evita gerar samples que seriam descartados e foi
  validada no PW5: o tempo médio de `retro_run()` caiu de 27,784 ms para
  11,858 ms, o FPS de emulação subiu de 33,5 para 55,2 e o uso de CPU caiu de
  98,2% para 71,0%.
- Essa alteração permanece pequena, isolada no adaptador e documentada como
  divergência local do SameBoy vendorizado. Uma atualização futura do
  submódulo deve reaplicar ou revisar essa decisão. O áudio só deve ser
  reativado quando o InkBoy tiver uma saída de áudio real.

## Mudanças futuras

- Remover o FBInk vendorizado exige uma alternativa validada no PW5 físico,
  preservando refresh, restauração e o fluxo de build/deployment.
- Alterar linguagem, biblioteca de display, política de refresh, threading ou
  integrar emulação exige uma decisão e validação próprias.

Para o histórico técnico e as evidências do FBInk, consulte
[`experiments/02-rendering.md`](experiments/02-rendering.md). Para as demais
evidências experimentais, consulte [`experiments/`](experiments/).
