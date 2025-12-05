%% SISTEMA DE CONTROLE ARC - PAINEL DE GERENCIAMENTO (FINAL V4)
% Funcionalidades:
% - Controle de Temperatura (Limiares 0x403)
% - Configuração Geral de Aquisição (Taxa + Start/Stop) - UNIFICADO
% - Máquina de Estados e Relés
% - Handshake Completo (9 Módulos)

clear; clc;
try stop(canChannelList); catch; end 

% --- CONFIGURAÇÕES GLOBAIS ---
HARDWARE_DEVICE = 'VN1630A 1';
HARDWARE_CHANNEL = 3; 
BAUD_RATE = 500000;

% Variáveis de Estado
masterCh = [];
sistemaValidado = false;
hLineMot = []; hLineAgu = []; 

% Loop do Menu Principal
opcao = -1;
while opcao ~= 0
    disp(' ');
    disp('==================================================');
    disp('       ARC - SISTEMA DE CONTROLE DE ARREFECIMENTO ');
    disp('==================================================');
    
    if ~isempty(masterCh) && isa(masterCh, 'can.Channel')
        fprintf('STATUS: [CONECTADO] em %s (Ch %d)\n', masterCh.Device, masterCh.DeviceChannelIndex);
    else
        fprintf('STATUS: [DESCONECTADO]\n');
    end
    
    disp('--------------------------------------------------');
    disp('1. CONECTAR Hardware (Vector)');
    disp('2. VERIFICAR MÓDULOS CAN');
    disp('3. INICIAR CONTROLE DE ARREFECIEMENTO  ');
    disp('4. MODO FILTRAGEM (Bomba ligada)');
    disp('5. MODO ARREFECIMENTO (Bomba + NA1/NF1)');
    disp('6. MODO DESCARTE (Bomba + NA2/NF2)');
    disp('7. MODO SNIFFER');
    disp('8. TESTE MANUAL DE RELÉS');
    disp('9. CONFIGURAR LIMIARES DE SEGURANÇA (0x403)');
    disp('10. CONFIGURAR AQUISIÇÃO GERAL DA CANOUTPUT '); 
    disp('0. SAIR');
    disp('==================================================');
    
    strOpcao = input('Digite a opção desejada: ', 's');
    opcao = str2double(strOpcao);
    clc;
    
    % Inicializa gráfico se necessário
    if ismember(opcao, [3, 4, 5, 6, 7]) && (isempty(hLineMot) || ~isvalid(hLineMot))
        fig = figure('Name', 'Monitoramento ARC', 'NumberTitle', 'off');
        hAx = axes;
        hLineMot = animatedline(hAx, 'Color', 'r', 'LineWidth', 2, 'DisplayName', 'Motor');
        hLineAgu = animatedline(hAx, 'Color', 'b', 'LineWidth', 2, 'DisplayName', 'Água');
        legend('show'); grid on; ylim([0 130]); title('MONITORAMENTO ATIVO');
        set(fig, 'CurrentCharacter', char(0));
    elseif ismember(opcao, [3, 4, 5, 6, 7])
        set(gcf, 'CurrentCharacter', char(0));
    end
    
    switch opcao
        case 1 % --- CONEXÃO ---
            disp('>>> Conectando ao hardware...');
            try
                if ~isempty(masterCh), stop(masterCh); end
                masterCh = canChannel('Vector', HARDWARE_DEVICE, HARDWARE_CHANNEL);
                configBusSpeed(masterCh, BAUD_RATE);
                start(masterCh);
                disp('-> Sucesso! Canal aberto.');
            catch ME
                disp('-> ERRO AO CONECTAR:'); disp(ME.message); masterCh = [];
            end
            
        case 2 % --- HANDSHAKE (COMPLETO 9 MÓDULOS) ---
            if isempty(masterCh), disp('ERRO: Conecte primeiro.'); continue; end
            disp('>>> Verificando rede (Solicitando RTRs)...');
            if masterCh.MessagesAvailable > 0, receive(masterCh, masterCh.MessagesAvailable); end
            
            % Lista completa de 9 IDs
            idsCheck = hex2dec({'401','510','520','530','610','611','620','621','622'});
            
            try
                for k=1:length(idsCheck)
                    msg = canMessage(idsCheck(k), false, 0); msg.Remote = true;
                    transmit(masterCh, msg); pause(0.02); 
                end
            catch, disp('AVISO: Falha no envio.'); end
            pause(1.0);
            
            if masterCh.MessagesAvailable > 0
                msgs = receive(masterCh, masterCh.MessagesAvailable);
                idsFound = unique([msgs(~[msgs.Remote] & ismember([msgs.ID], idsCheck)).ID]);
                
                fprintf('\nRESUMO DA REDE (%d/%d):\n', length(idsFound), length(idsCheck));
                checkID(idsFound, 1025, 'ARDUINO (0x401)');
                checkID(idsFound, 1296, 'CANTemp1TC (0x510)');
                checkID(idsFound, 1312, 'CANTemp2TC (0x520)');
                checkID(idsFound, 1328, 'CANTemp3TC (0x530)');
                checkID(idsFound, 1552, 'CANIn1AI1To4 (0x610)');
                checkID(idsFound, 1553, 'CANIn1AI5To8 (0x611)');
                checkID(idsFound, 1568, 'CANIn2AI1To2 (0x620)');
                checkID(idsFound, 1569, 'CANIn2DI1To4 (0x621)');
                checkID(idsFound, 1570, 'CANIn2PI1To2 (0x622)');
                
                if ismember(1025, idsFound) && ismember(1296, idsFound)
                    sistemaValidado = true; disp('-> SISTEMA VALIDADO.');
                else
                    sistemaValidado = false; disp('-> AVISO: Sensores críticos ausentes.');
                end
            else
                disp('-> NENHUMA RESPOSTA RECEBIDA.');
            end
            
        case 3 % --- AUTOMÁTICO ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            if ~sistemaValidado
                resp = input('AVISO: Handshake pendente. Continuar? (s/n): ', 's');
                if lower(resp) ~= 's', continue; end
            end
            
            disp(' ');
            disp('--- CONFIGURAÇÃO DE CONTROLE ---');
            useDefault = input('Usar padrões (90C/45C)? (s/n): ', 's');
            if lower(useDefault) == 'n'
                limMotor = input('Limite MOTOR (ex: 85): ');
                limAgua  = input('Limite ÁGUA  (ex: 50): ');
            else, limMotor = 90; limAgua = 45; end
            
            fprintf('>>> INICIANDO: Motor >= %.1f C | Água >= %.1f C\n', limMotor, limAgua);
            disp('Pressione "x" na janelinha para parar.'); pause(1.5);
            executarControle(masterCh, hAx, hLineMot, hLineAgu, limMotor, limAgua); 
            
        case 4 % --- FILTRAGEM ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            filtragem_agua(masterCh, hLineMot, hLineAgu);
            
        case 5 % --- ARREFECIMENTO ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            arrefecimento_func(masterCh, hLineMot, hLineAgu);
           
        case 6 % --- DESCARTE ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            Descarte_func(masterCh, hLineMot, hLineAgu);
            
        case 7 % --- SNIFFER ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            disp('>>> SNIFFER ATIVO (Pressione "x" no gráfico para sair)...');
            figKey = figure('Name','Pressione X para Sair','NumberTitle','off','MenuBar','none','Position',[100 100 300 50]);
            set(figKey, 'CurrentCharacter', char(0));
            while true
                if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', break; end
                if masterCh.MessagesAvailable > 0
                    msgs = receive(masterCh, masterCh.MessagesAvailable);
                    for k=1:length(msgs)
                        fprintf('0x%03X   | %s\n', msgs(k).ID, sprintf('%02X ', msgs(k).Data));
                    end
                end
                pause(0.05);
            end
            if isvalid(figKey), close(figKey); end
            
        case 8 % --- TESTE DE RELÉS ---
             if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            executarTesteReles(masterCh);
            
        case 9 % --- CONFIGURAR LIMIARES ---
             if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            configurarLimiaresPlaca(masterCh);
            
        case 10 % --- CONFIGURAÇÃO GERAL (TAXA + START/STOP) ---
             if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            configurarTaxaAquisicao(masterCh);
            
        case 0 % --- SAIR ---
            disp('Encerrando...'); stop(masterCh);
            
        otherwise
            disp('Opção Inválida.');
    end
    
    if ~ismember(opcao, [4, 5, 6, 7, 8])
        input('\nPressione Enter para continuar...');
    end
end

%% --- FUNÇÃO AUXILIAR DISPLAY ---
function checkID(foundList, id, name)
    if ismember(id, foundList), fprintf('[OK] %s\n', name);
    else, fprintf('[..] %s (Sem resposta)\n', name); end
end

%% --- FUNÇÃO 10: CONFIGURAR AQUISIÇÃO COMPLETA (0x404 + 0x405) ---
function configurarTaxaAquisicao(ch)
    disp('--------------------------------------------------');
    disp('    CONFIGURAÇÃO DE AQUISIÇÃO E CONTROLE (0x404/0x405)');
    disp('--------------------------------------------------');
    
    % --- PASSO 1: LEITURA ATUAL ---
    disp('>>> Lendo parâmetros atuais da placa...');
    if ch.MessagesAvailable > 0, receive(ch, ch.MessagesAvailable); end % Limpa buffer
    
    try
        % 1. Envia RTR para 0x404 (Config)
        msgRTR1 = canMessage(1028, false, 0); % 1028 = 0x404
        msgRTR1.Remote = true; 
        transmit(ch, msgRTR1);
        
        pause(0.1); % Pequeno intervalo
        
        % 2. Envia RTR para 0x405 (Status)
        msgRTR2 = canMessage(1029, false, 0); % 1029 = 0x405
        msgRTR2.Remote = true; 
        transmit(ch, msgRTR2);
        
    catch
        disp('Erro ao enviar RTR. Verifique a conexão.');
    end
    
    pause(0.5); % Aguarda resposta
    
    % Valores Padrão
    data404 = [0, 100, 1, 1, 0, 0, 0, 0]; % Timer=100ms (Big Endian)
    data405 = [0, 0, 0, 0, 0, 0, 0, 0];   % Parado
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for k=1:length(msgs)
            if ~msgs(k).Remote
                if msgs(k).ID == 1060, data404 = msgs(k).Data; end % Resp 0x424
                if msgs(k).ID == 1061, data405 = msgs(k).Data; end % Resp 0x425
            end
        end
    end
    
    % --- DECODIFICAÇÃO (BIG ENDIAN) ---
    % O Arduino manda MSB no Byte 0 e LSB no Byte 1
    currTimer = double(data404(1))*256 + double(data404(2));
    
    currAnalog = data404(3); % Byte 2
    currCont   = data404(4); % Byte 3
    
    % Status 0x405 (Bits 6-7 do Byte 0)
    currStatus = bitshift(data405(1), -6); 
    
    % Status Texto
    stAnalog = 'OFF'; if currAnalog == 1, stAnalog = 'ON '; end
    stCont   = 'OFF'; if currCont == 1,   stCont   = 'ON '; end
    stGeral  = 'PARADO '; if currStatus == 1, stGeral  = 'RODANDO'; end
    
    fprintf('\n   1. Taxa (Timer)..........: %d ms\n', currTimer);
    fprintf('   2. Leitura Analógica.....: [%s] (Val: %d)\n', stAnalog, currAnalog);
    fprintf('   3. Aquisição Contínua....: [%s] (Val: %d)\n', stCont, currCont);
    fprintf('   4. STATUS GERAL..........: [%s] (Val: %d)\n', stGeral, currStatus);
    disp('--------------------------------------------------');
    
    if lower(input('Deseja reconfigurar tudo? (s/n): ', 's')) == 's'
        disp('--- Digite os novos valores ---');
        
        % Inputs 0x404
        newTimer  = input('   Novo Tempo (ms) [10-3000]: ');
        newAnalog = input('   Habilitar Analógico? (1/0): ');
        newCont   = input('   Habilitar Contínuo?  (1/0): ');
        
        % Input 0x405
        newStart  = input('   STATUS DO SISTEMA (1=START, 0=STOP): ');
        
        % --- ENVIO 0x404 (CONFIGURAÇÃO) ---
        newData404 = data404;
        % Envia Timer em Big Endian para bater com a leitura
        newData404(1) = bitshift(newTimer, -8); % Byte 0: MSB
        newData404(2) = bitand(newTimer, 255);  % Byte 1: LSB
        newData404(3) = newAnalog;
        newData404(4) = newCont;
        
        msgConf = canMessage(1028, false, 8);
        msgConf.Data = newData404;
        transmit(ch, msgConf);
        fprintf('>>> Config 0x404 enviada: %s\n', sprintf('%02X ', newData404));
        
        pause(0.1);
        
        % --- ENVIO 0x405 (COMANDO) ---
        valToSend = bitshift(newStart, 6);
        
        msgCmd = canMessage(1029, false, 8);
        msgCmd.Data = [valToSend 0 0 0 0 0 0 0];
        transmit(ch, msgCmd);
        
        if newStart == 1
            disp('>>> COMANDO START (0x405) ENVIADO! Aquisição iniciada.');
        else
            disp('>>> COMANDO STOP (0x405) ENVIADO. Aquisição parada.');
        end
    else
        disp('Nenhuma alteração realizada.');
    end
end

%% --- FUNÇÃO 9: CONFIGURAR LIMIARES E TEMPOS (0x403 e 0x406) ---
function configurarLimiaresPlaca(ch)
    disp('--------------------------------------------------');
    disp('    CONFIGURAÇÃO DE SEGURANÇA (TEMP E TEMPO)');
    disp('    (Starter, Engine, Intercooler)');
    disp('--------------------------------------------------');
    
    % --- 1. LEITURA DO 0x403 (STARTER / ENGINE) ---
    disp('>>> Lendo 0x403 (Starter/Engine)...');
    if ch.MessagesAvailable > 0, receive(ch, ch.MessagesAvailable); end % Limpa buffer
    
    try
        msgRTR = canMessage(1027, false, 0); % 1027 = 0x403
        msgRTR.Remote = true; transmit(ch, msgRTR);
    catch, disp('Erro ao enviar RTR 0x403.'); end
    pause(0.5);
    
    % Dados Padrão 403 (Caso falhe a leitura)
    % Índices: 1=ID, 2=En, 3=TempS, 4=TimeS, 5=ID, 6=En, 7=TempE, 8=TimeE
    data403 = [12, 49, 80, 5, 0, 49, 105, 5]; 
    leu403 = false;
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for k=1:length(msgs)
            % A placa responde no ID 0x423 (1059)
            if ~msgs(k).Remote && msgs(k).ID == 1059 
                data403 = msgs(k).Data;
                leu403 = true;
                break;
            end
        end
    end
    
    % --- 2. LEITURA DO 0x406 (INTERCOOLER) ---
    disp('>>> Lendo 0x406 (Intercooler)...');
    try
        msgRTR2 = canMessage(1030, false, 0); % 1030 = 0x406
        msgRTR2.Remote = true; transmit(ch, msgRTR2);
    catch, disp('Erro ao enviar RTR 0x406.'); end
    pause(0.5);
    
    % Dados Padrão 406
    % Índices: 1=ID, 2=En, 3=TempI, 4=TimeI, ...
    data406 = [12, 49, 50, 5, 0, 0, 0, 0]; 
    leu406 = false;
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for k=1:length(msgs)
            % A placa responde no ID 0x426 (1062)
            if ~msgs(k).Remote && msgs(k).ID == 1062 
                data406 = msgs(k).Data;
                leu406 = true;
                break;
            end
        end
    end
    
    if ~leu403, disp('AVISO: Sem resposta do 0x403 (Usando padrão).'); end
    if ~leu406, disp('AVISO: Sem resposta do 0x406 (Usando padrão).'); end
    
    % --- EXIBIÇÃO DOS VALORES ATUAIS ---
    % Lê os bytes diretos (Sem conversão matemática)
    
    % STARTER (0x403)
    tempS = data403(3); 
    timeS = data403(4);
    
    % ENGINE (0x403)
    tempE = data403(7); 
    timeE = data403(8);
    
    % INTERCOOLER (0x406)
    tempI = data406(3); 
    timeI = data406(4);
    
    fprintf('\n   1. STARTER: %3d °C  | Tempo: %d s\n', tempS, timeS);
    fprintf('   2. ENGINE : %3d °C  | Tempo: %d s\n', tempE, timeE);
    fprintf('   3. INTERC.: %3d °C  | Tempo: %d s\n', tempI, timeI);
    disp('--------------------------------------------------');
    
    if lower(input('Alterar configurações? (s/n): ', 's')) == 's'
        disp('--- Insira os novos valores (0-255) ---');
        
        % Inputs STARTER
        nT_S = input('   Limite STARTER [C]: ');
        nt_S = input('   Tempo STARTER  [s]: ');
        
        % Inputs ENGINE
        nT_E = input('   Limite ENGINE  [C]: ');
        nt_E = input('   Tempo ENGINE   [s]: ');
        
        % Inputs INTERCOOLER
        nT_I = input('   Limite INTERC. [C]: ');
        nt_I = input('   Tempo INTERC.  [s]: ');
        
        % --- ENVIO (SEM CONVERSÃO, DIRETO) ---
        
        % Atualiza Buffer 0x403 (Starter/Engine)
        % Mantém Byte 1, 2, 5, 6 originais (ID/Enable)
        newData403 = data403;
        newData403(3) = nT_S; % Temp Starter
        newData403(4) = nt_S; % Time Starter
        newData403(7) = nT_E; % Temp Engine
        newData403(8) = nt_E; % Time Engine
        
        msg1 = canMessage(1027, false, 8); 
        msg1.Data = newData403;
        transmit(ch, msg1);
        fprintf('>>> Enviado 0x403: %s\n', sprintf('%02X ', newData403));
        
        pause(0.1);
        
        % Atualiza Buffer 0x406 (Intercooler)
        newData406 = data406;
        newData406(3) = nT_I; % Temp Intercooler
        newData406(4) = nt_I; % Time Intercooler
        
        msg2 = canMessage(1030, false, 8); 
        msg2.Data = newData406;
        transmit(ch, msg2);
        fprintf('>>> Enviado 0x406: %s\n', sprintf('%02X ', newData406));
        
        disp('>>> Configuração Concluída.');
    else
        disp('Nenhuma alteração realizada.');
    end
end

%% --- FUNÇÃO 1: MÁQUINA DE ESTADOS AUTOMÁTICA ---
function executarControle(ch, ax, lineM, lineA, limM, limA)
    disp('>>> CONTROLE AUTOMÁTICO INICIADO');
    figKey = figure('Name','Pressione X para Sair','NumberTitle','off','MenuBar','none','ToolBar','none','Position',[100 100 300 50]);
    set(figKey, 'CurrentCharacter', char(0));
    
    S1=1; S2=2; S3=3; S4=4;
    estado = S2; tempM = 0; tempA = 0; tempBoard = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    histM = limM - 3; histA = limA - 5;
    
    try
        while true
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', 
                disp('>>> Cancelado pelo usuário.'); break; 
            end
            
            tNow = (now - startTime) * 86400;
            if etime(clock, lastReq) > 0.2
                try, req = canMessage(1296, false, 0); req.Remote = true; transmit(ch, req);
                     req2 = canMessage(1025, false, 0); req2.Remote = true; transmit(ch, req2);
                catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    m = msgs(i);
                    if ~m.Remote && m.ID == 1296
                        d = uint16(m.Data);
                        % tempBoard = double(d(1)) - 128; 
                        rawTL = bitshift(d(2), -2) + bitshift(bitand(d(3), 63), 6);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTL) - 2048;
                        tempA = double(rawTR) - 2048;
                        addpoints(lineM, tNow, tempM); addpoints(lineA, tNow, tempA); drawnow limitrate;
                    end
                end
            end
            
            bomba=0; v1=0; v2=0; d1=0; d2=0;
            switch estado
                case S1
                    if tempM > 30, estado = S2; end
                case S2
                    bomba=1; if tempM >= limM, estado = S3; end
                case S3
                    bomba=1; v1=1;
                    if tempA >= limA, estado = S4; end
                    if tempM < histM, estado = S2; end
                case S4
                    bomba=1; v1=1; v2=1;
                    if tempM > (limM + 15), disp('ALERTA TEMP! D1/D2'); d1=1; d2=1; end
                    if tempM < histM && tempA < histA, estado = S3; end
            end
            title(ax, sprintf('AUTO S%d (M: %.1f | A: %.1f)', estado, tempM, tempA));
            
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                if d1, b1 = bitor(b1, 64); end
                if d2, b1 = bitor(b1, 16); end
                if v1, b2 = bitor(b2, 64); end 
                if v2, b2 = bitor(b2, 16); end 
                if bomba, b2 = bitor(b2, 1); end 
                
                cmd = canMessage(1026, false, 8); cmd.Data = [b1 b2 0 0 0 0 0 0];
                transmit(ch, cmd);
                
                stB='OFF'; if bomba, stB='ON '; end
                stV1='OFF'; if v1, stV1='ON '; end
                stV2='OFF'; if v2, stV2='ON '; end
                fprintf('T:%6.1fs|S%d| Mot:%5.1f| Agu:%5.1f || B:%s| V1:%s| V2:%s\n', ...
                    tNow, estado, tempM, tempA, stB, stV1, stV2);
                lastLog = clock;
            end
            pause(0.02);
        end
    catch, disp('Erro no loop.'); end
    if isvalid(figKey), close(figKey); end
end

% --- MANTER OUTRAS FUNÇÕES AUXILIARES IGUAIS ---
function filtragem_agua(ch, lineM, lineA)
    disp('>>> MODO FILTRAGEM INICIADO');
    figKey = figure('Name','Pressione X para Sair','NumberTitle','off','MenuBar','none','ToolBar','none','Position',[100 100 300 50]);
    set(figKey, 'CurrentCharacter', char(0));
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    
    try
        while true
            tNow = (now - startTime) * 86400;
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', 
                disp('>>> Cancelado pelo usuário.'); break; 
            end
            
            if etime(clock, lastReq) > 0.2
                try, req = canMessage(1296, false, 0); req.Remote = true; transmit(ch, req); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    m = msgs(i);
                    if ~m.Remote && m.ID == 1296
                        d = uint16(m.Data);
                        rawTL = bitshift(d(2), -2) + bitshift(bitand(d(3), 63), 6);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTL) - 2048;
                        tempA = double(rawTR) - 2048;
                        addpoints(lineM, tNow, tempM); addpoints(lineA, tNow, tempA); drawnow limitrate;
                    end
                end
            end
            
            bomba=1; 
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                if bomba, b2 = bitor(b2, 1); end 
                cmd = canMessage(1026, false, 8); cmd.Data = [b1 b2 0 0 0 0 0 0];
                transmit(ch, cmd);
                fprintf('FILTRAGEM | M:%5.1f C | A:%5.1f C | Bomba ON\n', tempM, tempA);
                lastLog = clock;
            end
            pause(0.02);
        end
    catch, disp('Erro.'); end
    if isvalid(figKey), close(figKey); end
end

function arrefecimento_func(ch, lineM, lineA)
    disp('>>> MODO ARREFECIMENTO INICIADO');
    figKey = figure('Name','Pressione X para Sair','NumberTitle','off','MenuBar','none','ToolBar','none','Position',[100 100 300 50]);
    set(figKey, 'CurrentCharacter', char(0));
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    
    try
        while true
            tNow = (now - startTime) * 86400;
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', break; end
            
            if etime(clock, lastReq) > 0.2
                try, req = canMessage(1296, false, 0); req.Remote = true; transmit(ch, req); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    m = msgs(i);
                    if ~m.Remote && m.ID == 1296
                        d = uint16(m.Data);
                        rawTL = bitshift(d(2), -2) + bitshift(bitand(d(3), 63), 6);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTL) - 2048;
                        tempA = double(rawTR) - 2048;
                        addpoints(lineM, tNow, tempM); addpoints(lineA, tNow, tempA); drawnow limitrate;
                    end
                end
            end
            
            bomba=1; v1=1;
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                if bomba, b2 = bitor(b2, 1); end 
                if v1, b2 = bitor(b2, 64); end 
                cmd = canMessage(1026, false, 8); cmd.Data = [b1 b2 0 0 0 0 0 0];
                transmit(ch, cmd);
                fprintf('ARREFECIM.| M:%5.1f C | A:%5.1f C | Bomba + NA1\n', tempM, tempA);
                lastLog = clock;
            end
            pause(0.02);
        end
    catch, disp('Erro.'); end
    if isvalid(figKey), close(figKey); end
end

function Descarte_func(ch, lineM, lineA)
    disp('>>> MODO DESCARTE INICIADO');
    figKey = figure('Name','Pressione X para Sair','NumberTitle','off','MenuBar','none','ToolBar','none','Position',[100 100 300 50]);
    set(figKey, 'CurrentCharacter', char(0));
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    
    try
        while true
            tNow = (now - startTime) * 86400;
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', break; end
            
            if etime(clock, lastReq) > 0.2
                try, req = canMessage(1296, false, 0); req.Remote = true; transmit(ch, req); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    m = msgs(i);
                    if ~m.Remote && m.ID == 1296
                        d = uint16(m.Data);
                        rawTL = bitshift(d(2), -2) + bitshift(bitand(d(3), 63), 6);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTL) - 2048;
                        tempA = double(rawTR) - 2048;
                        addpoints(lineM, tNow, tempM); addpoints(lineA, tNow, tempA); drawnow limitrate;
                    end
                end
            end
            
            bomba=1; v1=1; v2=1;
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                if bomba, b2 = bitor(b2, 1); end 
                if v1, b2 = bitor(b2, 64); end 
                if v2, b2 = bitor(b2, 16); end 
                cmd = canMessage(1026, false, 8); cmd.Data = [b1 b2 0 0 0 0 0 0];
                transmit(ch, cmd);
                fprintf('DESCARTE  | M:%5.1f C | A:%5.1f C | TUDO ABERTO\n', tempM, tempA);
                lastLog = clock;
            end
            pause(0.02);
        end
    catch, disp('Erro.'); end
    if isvalid(figKey), close(figKey); end
end

function executarTesteReles(ch)
    reles = zeros(1, 8); sairTeste = false;
    while ~sairTeste
        clc; disp('=== TESTE DE RELÉS (ID 0x402) ===');
        b1 = 0;
        if reles(1), b1 = bitor(b1, 64); end 
        if reles(2), b1 = bitor(b1, 16); end 
        if reles(3), b1 = bitor(b1, 4);  end 
        if reles(4), b1 = bitor(b1, 1);  end 
        b2 = 0;
        if reles(5), b2 = bitor(b2, 64); end 
        if reles(6), b2 = bitor(b2, 16); end 
        if reles(7), b2 = bitor(b2, 4);  end 
        if reles(8), b2 = bitor(b2, 1);  end 
        msg = canMessage(1026, false, 8); msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
        try, transmit(ch, msg); catch, end
        fprintf('ENVIANDO: %02X %02X\n', b1, b2);
        disp('----------------------------------------');
        for i = 1:8
            st = '[ OFF ]'; if reles(i), st = '[ ON  ] LIGADO'; end
            nome = sprintf('Saída D%d', i);
            if i==5, nome='NA1/NF1'; end
            if i==6, nome='NA2/NF2'; end
            if i==8, nome='Bomba'; end
            fprintf(' %d. %s -> %s\n', i, st, nome);
        end
        disp('----------------------------------------');
        disp(' 9. LIGAR TUDO'); disp('10. DESLIGAR TUDO'); disp(' x. VOLTAR');
        inp = input('Opção: ', 's');
        if strcmpi(inp, 'x')
            msg.Data = [0 0 0 0 0 0 0 0]; transmit(ch, msg); sairTeste = true;
        else
            val = str2double(inp);
            if ~isnan(val)
                if val >= 1 && val <= 8, reles(val) = ~reles(val);
                elseif val == 9, reles(:) = 1;
                elseif val == 10, reles(:) = 0; end
            end
        end
    end
end