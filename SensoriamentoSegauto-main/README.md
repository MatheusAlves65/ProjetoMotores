# SensoriamentoSegauto
Timers 1/2/4 estão sendo utilizados para controle pwm das ponte h.
Timer 5 usado pelo PWM nos pinos 44/46
Utilizando da biblioteca Timer interrupt se cria 2 ISR interrupt timer utilizando-se apenas do timer5, Erro encontrado foi de +-1ms.
Configurado igual agora é capaz de fazer intervalos de 10ms à 10s com exatidão de 1ms.
