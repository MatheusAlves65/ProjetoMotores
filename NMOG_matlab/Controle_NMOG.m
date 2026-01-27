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
    disp('2. VERIFICAR MÓDULOS CAN (Handshake)');
    disp('3. INICIAR CONTROLE AUTOMÁTICO');
    disp('4. MODO FILTRAGEM (Bomba ligada)');
    disp('5. MODO ARREFECIMENTO (Bomba + NA1)');
    disp('6. MODO DESCARTE (Tudo Aberto)');
    disp('7. MODO SNIFFER');
    disp('8. TESTE MANUAL DE RELÉS');
    disp('9. CONFIGURAR LIMIARES DE SEGURANÇA (0x403)');
    disp('10. CONFIGURAR AQUISIÇÃO GERAL (0x404)');
    disp('11. MONITORAR MÓDULOS TÉRMICOS (510, 520, 530)'); % NOVA OPÇÃO
    disp('0. SAIR');
    disp('==================================================');
    
    strOpcao = input('Digite a opção desejada: ', 's');
    opcao = str2double(strOpcao);
    clc;
    
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
            
        case 2 % --- HANDSHAKE ---
            if isempty(masterCh), disp('ERRO: Conecte primeiro.'); continue; end
            disp('>>> Verificando rede (Solicitando RTRs)...');
            if masterCh.MessagesAvailable > 0, receive(masterCh, masterCh.MessagesAvailable); end
            
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
            useDefault = input('Usar padrões (90C/45C)? (s/n): ', 's');
            if lower(useDefault) == 'n'
                limMotor = input('Limite MOTOR (ex: 85): ');
                limAgua  = input('Limite ÁGUA  (ex: 50): ');
            else, limMotor = 90; limAgua = 45; end
            executarControle(masterCh,limMotor, limAgua); 
            
        case 4 % --- FILTRAGEM ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            filtragem_agua(masterCh);
            
        case 5 % --- ARREFECIMENTO ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            arrefecimento_func(masterCh);
           
        case 6 % --- DESCARTE ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            Descarte_func(masterCh);
            
        case 7 % --- SNIFFER ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            disp('>>> SNIFFER ATIVO (Pressione "x" na janela para sair)...');
            % (Código do sniffer mantido simples aqui ou chame função se tiver)
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
            
        case 10 % --- CONFIGURAÇÃO GERAL ---
             if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            configurarTaxaAquisicao(masterCh);

        case 11 % --- NOVO: MONITORAR TÉRMICOS ---
             if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            monitorarModulosTermicos(masterCh);
            
        case 0 % --- SAIR ---
            disp('Encerrando...');try stop(masterCh);catch, end
            
        otherwise
            disp('Opção Inválida.');
    end
    
    if ~ismember(opcao, [4, 5, 6, 7, 8, 11])
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

%% --- FUNÇÃO 9: CONFIGURAR LIMIARES E HABILITAÇÃO (STARTER, ENGINE, INTERC, WATER) ---
function configurarLimiaresPlaca(ch)
    disp('--------------------------------------------------');
    disp('    CONFIGURAÇÃO DE SEGURANÇA (STATUS, TEMP, TEMPO)');
    disp('    (1=Habilitado/ON, 3=Desabilitado/OFF)');
    disp('--------------------------------------------------');
    
    % --- 1. LEITURA DO 0x403 (STARTER / ENGINE) ---
    disp('>>> Lendo 0x403 (Starter/Engine)...');
    if ch.MessagesAvailable > 0, receive(ch, ch.MessagesAvailable); end
    
    try
        msgRTR = canMessage(1027, false, 0); % 0x403
        msgRTR.Remote = true; transmit(ch, msgRTR);
    catch, disp('Erro ao enviar RTR 0x403.'); end
    pause(0.5);
    
    % Padrão 0x403 (Bytes 2 e 6 são Enable)
    data403 = [12, 3, 80, 5, 0, 3, 105, 5]; 
    leu403 = false;
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for k=1:length(msgs)
            if ~msgs(k).Remote && msgs(k).ID == 1059 % Resp 0x423
                data403 = msgs(k).Data;
                fprintf('0x%03X   | %s\n', msgs(k).ID, sprintf('%02X ', msgs(k).Data));
                leu403 = true;
                break;
            end
        end
    end
    
    % --- 2. LEITURA DO 0x406 (INTERCOOLER / WATER) ---
    disp('>>> Lendo 0x406 (Intercooler/Water)...');
    try
        msgRTR2 = canMessage(1030, false, 0); % 0x406
        msgRTR2.Remote = true; transmit(ch, msgRTR2);
    catch, disp('Erro ao enviar RTR 0x406.'); end
    pause(0.5);
    
    % Padrão 0x406 (Bytes 2 e 6 são Enable)
    data406 = [12, 3, 50, 5, 0, 3, 95, 5]; 
    leu406 = false;
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for k=1:length(msgs)
            if ~msgs(k).Remote && msgs(k).ID == 1062 % Resp 0x426
                data406 = msgs(k).Data;
                fprintf('0x%03X   | %s\n', msgs(k).ID, sprintf('%02X ', msgs(k).Data));
                leu406 = true;
                break;
            end
        end
    end
    
    % --- DECODIFICAÇÃO E STATUS ---
    % Função auxiliar simples para texto (1=ON, 3=OFF)
    getTxt = @(x) char("ON " * (x==1) + "OFF" * (x~=1));
    
    % 0x403: STARTER
    enS = bitand(data403(2),15); tempS = data403(3); timeS = data403(4);
    stS = 'OFF';
    if enS == 1, stS = 'ON '; end
    if enS == 3, stS = 'OFF '; end
    % 0x403: ENGINE
    enE = bitand(data403(6),15); tempE = data403(7); timeE = data403(8);
        fprintf('enE: %3d', enE);
     if enE == 1, stE = 'ON '; end
     if enE == 3, stE = 'OFF'; end
    
    % 0x406: INTERCOOLER
    enI = bitand(data406(2),15); tempI = data406(3); timeI = data406(4);
    fprintf('enI: %3d', enI);
    if enI == 1 
        stI = 'ON';
    elseif  enI == 3
        stI = 'OFF';
    end
    
    % 0x406: WATER
    enW = bitand(data406(6),15); tempW = data406(7); timeW = data406(8);
    fprintf('enW: %3d', enW)
    stW = 'OFF'; if enW == 1, stW = 'ON '; end
    
    % --- EXIBIÇÃO ---
    fprintf('\n   1. STARTER: [%s] (Val:%d) | %2d °C | %d s\n', stS, enS, tempS, timeS);
    fprintf('   2. ENGINE : [%s] (Val:%d) | %3d °C | %d s\n', stE, enE, tempE, timeE);
    fprintf('   3. INTERC.: [%s] (Val:%d) | %3d °C | %d s\n', stI, enI, tempI, timeI);
    fprintf('   4. WATER  : [%s] (Val:%d) | %3d °C | %d s\n', stW, enW, tempW, timeW);
    disp('--------------------------------------------------');
    
    if lower(input('Alterar configurações? (s/n): ', 's')) == 's'
        disp('--- Digite os novos valores ---');
        disp(' (Para Enable: 1 = Ligar, 3 = Desligar)');
        
        % --- INPUTS STARTER ---
        nEn_S = input('   STARTER Habilitado? (1/3): ');
        nT_S  = input('   STARTER Limite [C]: ');
        nt_S  = input('   STARTER Tempo  [s]: ');
        
        % --- INPUTS ENGINE ---
        nEn_E = input('   ENGINE  Habilitado? (1/3): ');
        nT_E  = input('   ENGINE  Limite [C]: ');
        nt_E  = input('   ENGINE  Tempo  [s]: ');
        
        % --- INPUTS INTERCOOLER ---
        nEn_I = input('   INTERC. Habilitado? (1/3): ');
        nT_I  = input('   INTERC. Limite [C]: ');
        nt_I  = input('   INTERC. Tempo  [s]: ');
        
        % --- INPUTS WATER ---
        nEn_W = input('   WATER   Habilitado? (1/3): ');
        nT_W  = input('   WATER   Limite [C]: ');
        nt_W  = input('   WATER   Tempo  [s]: ');
        
        % --- ATUALIZAÇÃO E ENVIO ---
        
        % Config 0x403
        newData403 = data403;
        newData403(2) = (0x30||(nEn_S && 0x03)); newData403(3) = nT_S; newData403(4) = nt_S;
        newData403(6) = (0x30||(nEn_E && 0x03)); newData403(7) = nT_E; newData403(8) = nt_E;
        fprintf('newData403(2): %02X',newData403(2));
        fprintf('newData403(6): %02X',newData403(6));
        msg1 = canMessage(1027, false, 8); 
        msg1.Data = newData403;
        transmit(ch, msg1);
        fprintf('>>> Enviado 0x403: %s\n', sprintf('%02X ', newData403));
        
        pause(0.1);
        
        % Config 0x406
        newData406 = data406;
        newData406(2) = nEn_I; newData406(3) = nT_I; newData406(4) = nt_I;
        newData406(6) = nEn_W; newData406(7) = nT_W; newData406(8) = nt_W;
        
        msg2 = canMessage(1030, false, 8); 
        msg2.Data = newData406;
        transmit(ch, msg2);
        fprintf('>>> Enviado 0x406: %s\n', sprintf('%02X ', newData406));
        
        disp('>>> Configuração Concluída.');
    else
        disp('Nenhuma alteração realizada.');
    end
end
%% --- FUNÇÃO 1: MÁQUINA DE ESTADOS (CORRIGIDA E SEGURA) ---
function executarControle(ch, limM, limA)
    disp('>>> CONTROLE AUTOMÁTICO INICIADO');
    disp('    Lógica idêntica ao Teste Manual.');
    disp('    -------------------------------------------------');
    disp('    PARA VOLTAR AO MENU: Feche a janela "CONTROLE ATIVO"');
    disp('    -------------------------------------------------');
    
    % --- JANELA DE CONTROLE ---
    figKey = figure('Name','CONTROLE ATIVO - Feche para Voltar','NumberTitle','off',...
                    'MenuBar','none','ToolBar','none',...
                    'Position',[100 100 400 100], 'Color', [0.9 0.4 0.4]); 
    
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA VOLTAR AO MENU',...
              'Position',[20 20 360 60], 'BackgroundColor',[0.9 0.4 0.4],...
              'FontSize', 12, 'FontWeight', 'bold', 'ForegroundColor', 'white');
              
    set(figKey, 'CurrentCharacter', char(0));
    
    % --- GATILHO DE SEGURANÇA ---
    cleanupObj = onCleanup(@() desligarSeguro(ch));
    
    % Estados
    S1=1; S2=2; S3=3; S4=4;
    
    % Inicia no Estado 2 (Bomba Ligada)
    estado = S2; 
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    histM = limM - 3; histA = limA - 5;
    
    try
        while true
            % --- SAÍDA ---
            if ~isvalid(figKey)
                disp('>>> Janela fechada. Voltando ao Menu...');
                break; 
            end
            
            if lower(get(figKey, 'CurrentCharacter')) == 'x'
                disp('>>> Comando de saída recebido. Voltando ao Menu...');
                break;
            end
            
            tNow = (now - startTime) * 86400;
            
            % --- LEITURA ---
            if etime(clock, lastReq) > 0.2
                try, transmit(ch, canMessage(1296, false, 0, 'Remote', true)); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    if ~msgs(i).Remote && msgs(i).ID == 1296
                        d = uint16(msgs(i).Data);
                         rawBR = bitshift(bitand(d(7),240), -4) + bitshift(d(8),4);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTR) - 2048; tempA = double(rawBR) - 2048;
                    end
                end
            end
            
            % --- LÓGICA DE ESTADOS ---
            bomba=0; v1=0; v2=0; d1=0; d2=0;
            
            switch estado
                case S1 % Repouso
                    if tempM > 0, estado = S2; end
                case S2 % Filtragem
                    bomba=1; 
                    if tempM >= limM, estado = S3; end
                case S3 % Circulação
                    bomba=1; v1=1; 
                    if tempA >= limA, estado = S4; end
                    if tempM < histM, estado = S2; end
                case S4 % Descarte
                    bomba=1; v1=1; v2=1;
                    if tempM < histM && tempA < histA, estado = S3; end
            end
            
            if tempM > (limM + 15), d1=1; d2=1; end
            
            % --- ENVIO (LÓGICA MANUAL) ---
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                
                % Byte 1 
                if d1 == 0, b1 = bitor(b1, 64); end 
                if d2 == 0, b1 = bitor(b1, 16); end 
                b1 = bitor(b1, 4); b1 = bitor(b1, 1);
                
                % Byte 2
                if v2 == 0, b2 = bitor(b2, 64); end 
                if v1 == 0, b2 = bitor(b2, 16); end 
                b2 = bitor(b2, 4); 
                if bomba == 0, b2 = bitor(b2, 1); end 
                
                try
                    cmd = canMessage(1026, false, 8); 
                    cmd.Data = [b1 b2 0 0 0 0 0 0];
                    transmit(ch, cmd);
                catch
                end
                
                stB='OFF'; if bomba, stB='ON '; end
                fprintf('T:%6.1fs | S%d | Motor:%5.1f|Agua: %5.1f | B:%s | V1:%d | V2:%d\n', ...
                    tNow, estado, tempM,tempA, stB, v1, v2);
                
                lastLog = clock;
            end
            pause(0.02);
        end
    catch ME
        disp(['Erro no loop: ' ME.message]);
    end
    
    if isvalid(figKey), close(figKey); end
end

% --- FUNÇÃO DE SEGURANÇA (CORRIGIDA) ---
function desligarSeguro(ch)
    % Verifica canal
    if ~isempty(ch) && isvalid(ch) && ch.Running
        try
            % CORREÇÃO: Cria mensagem primeiro, atribui dados depois
            msg = canMessage(1026, false, 8);
            msg.Data = [85, 85, 0, 0, 0, 0, 0, 0]; % 85 = 01010101 (Desliga tudo)
            
            transmit(ch, msg);
            pause(0.05);
            
            fprintf('\n>>> HARDWARE DESLIGADO (Retornando ao Menu).\n');
        catch ME
            fprintf('\nAVISO: Erro ao enviar desligamento: %s\n', ME.message);
        end
    else
        fprintf('\nAVISO: Canal fechado. Não foi possível desligar hardware.\n');
    end
end
%% --- FUNÇÃO 4: MODO FILTRAGEM (SOMENTE BOMBA LIGADA) ---
function filtragem_agua(ch)
    disp('>>> MODO FILTRAGEM INICIADO');
    disp('    Estado: BOMBA ON | VÁLVULAS OFF');
    disp('    -------------------------------------------------');
    disp('    PARA VOLTAR AO MENU: Feche a janela "FILTRAGEM ATIVA"');
    disp('    -------------------------------------------------');
    
    % --- JANELA DE CONTROLE ---
    figKey = figure('Name','FILTRAGEM ATIVA - Feche para Voltar','NumberTitle','off',...
                    'MenuBar','none','ToolBar','none',...
                    'Position',[100 100 400 100], 'Color', [0.4 0.4 0.9]); % Azulado para diferenciar
    
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA PARAR A FILTRAGEM',...
              'Position',[20 20 360 60], 'BackgroundColor',[0.4 0.4 0.9],...
              'FontSize', 12, 'FontWeight', 'bold', 'ForegroundColor', 'white');
              
    set(figKey, 'CurrentCharacter', char(0));
    
    % --- GATILHO DE SEGURANÇA ---
    cleanupObj = onCleanup(@() desligarSeguro_filt(ch));
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    
    try
        while true
            % --- SAÍDA ---
            if ~isvalid(figKey)
                disp('>>> Janela fechada. Voltando ao Menu...');
                break; 
            end
            
            if lower(get(figKey, 'CurrentCharacter')) == 'x'
                disp('>>> Comando de saída recebido. Voltando ao Menu...');
                break;
            end
            
            tNow = (now - startTime) * 86400;
            
            % --- LEITURA (Apenas para monitoramento) ---
            if etime(clock, lastReq) > 0.2
                try, transmit(ch, canMessage(1296, false, 0, 'Remote', true)); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    if ~msgs(i).Remote && msgs(i).ID == 1296
                        d = uint16(msgs(i).Data);
                        rawBR = bitshift(bitand(d(7),240), -4) + bitshift(d(8),4);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTR) - 2048; tempA = double(rawBR) - 2048;
                    end
                end
            end
            
            % --- ENVIO (LÓGICA INVERSA MANUAL) ---
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                
                % --- BYTE 1: TUDO DESLIGADO ---
                % Bits: 0, 2, 4, 6 colocados em 1 (OFF)
                b1 = bitor(b1, 64); % D1 OFF
                b1 = bitor(b1, 16); % D2 OFF
                b1 = bitor(b1, 4);  % D3 OFF
                b1 = bitor(b1, 1);  % D4 OFF
                
                % --- BYTE 2: SÓ BOMBA LIGADA ---
                b2 = bitor(b2, 64); % V2 (NA2) OFF
                b2 = bitor(b2, 16); % V1 (NA1) OFF
                b2 = bitor(b2, 4);  % D7 OFF
                
                % BOMBA (Bit 0): 
                % Como queremos LIGADA, NÃO fazemos o bitor com 1.
                % Deixamos o bit 0 como '0'.
                
                try
                    cmd = canMessage(1026, false, 8); 
                    cmd.Data = [b1 b2 0 0 0 0 0 0];
                    transmit(ch, cmd);
                catch
                end
                
                fprintf('FILTRAGEM | Tempo: %6.1fs | Mot:%5.1f C | Agu:%5.1f C | BOMBA LIGADA\n', ...
                    tNow, tempM, tempA);
                
                lastLog = clock;
            end
            pause(0.02);
        end
    catch ME
        disp(['Erro no loop: ' ME.message]);
    end
    
    if isvalid(figKey), close(figKey); end
end

% --- REUTILIZAÇÃO DA FUNÇÃO DE SEGURANÇA ---
% (Copie isso se não estiver no final do arquivo, ou deixe apenas uma vez no final do script principal)
function desligarSeguro_filt(ch)
    if ~isempty(ch) && isvalid(ch) && ch.Running
        try
            msg = canMessage(1026, false, 8);
            msg.Data = [85, 85, 0, 0, 0, 0, 0, 0];
            transmit(ch, msg); pause(0.05);
            fprintf('\n>>> HARDWARE DESLIGADO (Retornando ao Menu).\n');
        catch, end
    end
end

%% --- FUNÇÃO 5: MODO ARREFECIMENTO (BOMBA + V1 LIGADOS) ---
function arrefecimento_func(ch)
    disp('>>> MODO ARREFECIMENTO INICIADO');
    disp('    Estado: BOMBA ON | NA1 (Circulação) ON | NA2 OFF');
    disp('    -------------------------------------------------');
    disp('    PARA VOLTAR AO MENU: Feche a janela "ARREFECIMENTO ATIVO"');
    disp('    -------------------------------------------------');
    
    % --- JANELA DE CONTROLE ---
    figKey = figure('Name','ARREFECIMENTO ATIVO - Feche para Voltar','NumberTitle','off',...
                    'MenuBar','none','ToolBar','none',...
                    'Position',[100 100 400 100], 'Color', [0.4 0.8 0.4]); % Esverdeado
    
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA PARAR',...
              'Position',[20 20 360 60], 'BackgroundColor',[0.4 0.8 0.4],...
              'FontSize', 12, 'FontWeight', 'bold', 'ForegroundColor', 'white');
              
    set(figKey, 'CurrentCharacter', char(0));
    
    % --- GATILHO DE SEGURANÇA ---
    cleanupObj = onCleanup(@() desligarSeguro_arref(ch));
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    
    try
        while true
            % --- VERIFICAÇÃO DE SAÍDA ---
            if ~isvalid(figKey)
                disp('>>> Janela fechada. Voltando ao Menu...');
                break; 
            end
            
            if lower(get(figKey, 'CurrentCharacter')) == 'x'
                disp('>>> Comando de saída recebido. Voltando ao Menu...');
                break;
            end
            
            tNow = (now - startTime) * 86400;
            
            % --- LEITURA ---
            if etime(clock, lastReq) > 0.2
                try, transmit(ch, canMessage(1296, false, 0, 'Remote', true)); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    if ~msgs(i).Remote && msgs(i).ID == 1296
                        d = uint16(msgs(i).Data);
                        rawBR = bitshift(bitand(d(7),240), -4) + bitshift(d(8),4);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTR) - 2048; tempA = double(rawBR) - 2048;
                    end
                end
            end
            
            % --- ENVIO (LÓGICA INVERSA MANUAL) ---
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                
                % --- BYTE 1: TUDO DESLIGADO (Manda 1) ---
                b1 = bitor(b1, 64); % D1 OFF
                b1 = bitor(b1, 16); % D2 OFF
                b1 = bitor(b1, 4);  % D3 OFF
                b1 = bitor(b1, 1);  % D4 OFF
                
                % --- BYTE 2: CONFIGURAÇÃO ARREFECIMENTO ---
                
                % V2 (NA2/Descarte - Bit 6): DESLIGADA -> Manda 1
                b2 = bitor(b2, 64); 
                
                % V1 (NA1/Circulação - Bit 4): LIGADA -> Manda 0 (Não faz nada)
                % (Não adicionamos 16 aqui, deixando o bit em 0)
                
                % D7 (Bit 2): DESLIGADO -> Manda 1
                b2 = bitor(b2, 4);
                
                % BOMBA (Bit 0): LIGADA -> Manda 0 (Não faz nada)
                % (Não adicionamos 1 aqui, deixando o bit em 0)
                
                try
                    cmd = canMessage(1026, false, 8); 
                    cmd.Data = [b1 b2 0 0 0 0 0 0];
                    transmit(ch, cmd);
                catch
                end
                
                fprintf('ARREFECIM.| T:%6.1fs | M:%5.1f | A:%5.1f | BOMBA + V1 (NA1) ON\n', ...
                    tNow, tempM, tempA);
                
                lastLog = clock;
            end
            pause(0.02);
        end
    catch ME
        disp(['Erro no loop: ' ME.message]);
    end
    
    if isvalid(figKey), close(figKey); end
end


function desligarSeguro_arref(ch)
    if ~isempty(ch) && isvalid(ch) && ch.Running
        try
            msg = canMessage(1026, false, 8);
            msg.Data = [85, 85, 0, 0, 0, 0, 0, 0];
            transmit(ch, msg); pause(0.05);
            fprintf('\n>>> HARDWARE DESLIGADO (Retornando ao Menu).\n');
        catch, end
    end
end
%% DESCARTE
%% --- FUNÇÃO 6: MODO DESCARTE (TUDO LIGADO) ---
function Descarte_func(ch)
    disp('>>> MODO DESCARTE INICIADO');
    disp('    Estado: BOMBA ON | V1 ON | V2 ON (Esvaziando...)');
    disp('    -------------------------------------------------');
    disp('    PARA VOLTAR AO MENU: Feche a janela "DESCARTE ATIVO"');
    disp('    -------------------------------------------------');
    
    % --- JANELA DE CONTROLE ---
    % Cor Laranja/Vermelha para indicar atenção (Descarte)
    figKey = figure('Name','DESCARTE ATIVO - Feche para Voltar','NumberTitle','off',...
                    'MenuBar','none','ToolBar','none',...
                    'Position',[100 100 400 100], 'Color', [0.9 0.6 0.2]); 
    
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA PARAR O DESCARTE',...
              'Position',[20 20 360 60], 'BackgroundColor',[0.9 0.6 0.2],...
              'FontSize', 12, 'FontWeight', 'bold', 'ForegroundColor', 'white');
              
    set(figKey, 'CurrentCharacter', char(0));
    
    % --- GATILHO DE SEGURANÇA ---
    cleanupObj = onCleanup(@() desligarSeguro_desc(ch));
    
    tempM = 0; tempA = 0;
    lastLog = clock; lastReq = clock; startTime = now;
    
    try
        while true
            % --- VERIFICAÇÃO DE SAÍDA ---
            if ~isvalid(figKey)
                disp('>>> Janela fechada. Voltando ao Menu...');
                break; 
            end
            
            if lower(get(figKey, 'CurrentCharacter')) == 'x'
                disp('>>> Comando de saída recebido. Voltando ao Menu...');
                break;
            end
            
            tNow = (now - startTime) * 86400;
            
            % --- LEITURA ---
            if etime(clock, lastReq) > 0.2
                try, transmit(ch, canMessage(1296, false, 0, 'Remote', true)); catch, end
                lastReq = clock;
            end
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                for i=1:length(msgs)
                    if ~msgs(i).Remote && msgs(i).ID == 1296
                        d = uint16(msgs(i).Data);
                        rawBR = bitshift(bitand(d(7),240), -4) + bitshift(d(8),4);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        tempM = double(rawTR) - 2048; tempA = double(rawBR) - 2048;
                    end
                end
            end
            
            % --- ENVIO (LÓGICA INVERSA MANUAL) ---
            if etime(clock, lastLog) > 0.5
                b1 = 0; b2 = 0;
                
                % --- BYTE 1: TUDO DESLIGADO (Manda 1) ---
                b1 = bitor(b1, 64); % D1 OFF
                b1 = bitor(b1, 16); % D2 OFF
                b1 = bitor(b1, 4);  % D3 OFF
                b1 = bitor(b1, 1);  % D4 OFF
                
                % --- BYTE 2: TUDO LIGADO (Bomba + V1 + V2) ---
                
                % V2 (NA2/Descarte - Bit 6): Queremos ON (0).
                % NÃO fazemos bitor. Deixa 0.
                
                % V1 (NA1/Circulação - Bit 4): Queremos ON (0).
                % NÃO fazemos bitor. Deixa 0.
                
                % D7 (Bit 2): Queremos OFF (1).
                b2 = bitor(b2, 4);
                
                % BOMBA (Bit 0): Queremos ON (0).
                % NÃO fazemos bitor. Deixa 0.
                
                try
                    cmd = canMessage(1026, false, 8); 
                    cmd.Data = [b1 b2 0 0 0 0 0 0];
                    transmit(ch, cmd);
                catch
                end
                
                fprintf('DESCARTE  | T:%6.1fs | M:%5.1f | A:%5.1f | TUDO ABERTO (ON)\n', ...
                    tNow, tempM, tempA);
                
                lastLog = clock;
            end
            pause(0.02);
        end
    catch ME
        disp(['Erro no loop: ' ME.message]);
    end
    
    if isvalid(figKey), close(figKey); end
end

function desligarSeguro_desc(ch)
    if ~isempty(ch) && isvalid(ch) && ch.Running
        try
            msg = canMessage(1026, false, 8);
            msg.Data = [85, 85, 0, 0, 0, 0, 0, 0];
            transmit(ch, msg); pause(0.05);
            fprintf('\n>>> HARDWARE DESLIGADO (Retornando ao Menu).\n');
        catch, end
    end
end

%% --- FUNÇÃO 8: TESTE DE RELÉS (LÓGICA INVERSA: 0=LIGA, 1=DESLIGA) ---
function executarTesteReles(ch)
    % Estado visual: 0 = OFF na tela, 1 = ON na tela
    reles = zeros(1, 8); 
    sairTeste = false;
    
    while ~sairTeste
        clc; disp('=== TESTE DE RELÉS (ID 0x402) - LÓGICA INVERSA ===');
        disp(' Nota: Enviando 0 para LIGAR e 1 para DESLIGAR o hardware');
        
        % --- MONTAGEM DOS BYTES (Lógica Invertida) ---
        % Se o relé deve estar DESLIGADO (0), enviamos bit 1.
        % Se o relé deve estar LIGADO (1), enviamos bit 0.
        
        b1 = 0;
        % Byte 1 (Relés 1 a 4)
        if reles(1) == 0, b1 = bitor(b1, 64); end % Se OFF na tela, manda 1
        if reles(2) == 0, b1 = bitor(b1, 16); end
        if reles(3) == 0, b1 = bitor(b1, 4);  end
        if reles(4) == 0, b1 = bitor(b1, 1);  end 
        
        b2 = 0;
        % Byte 2 (Relés 5 a 8)
        if reles(5) == 0, b2 = bitor(b2, 64); end 
        if reles(6) == 0, b2 = bitor(b2, 16); end 
        if reles(7) == 0, b2 = bitor(b2, 4);  end 
        if reles(8) == 0, b2 = bitor(b2, 1);  end 
        
        % Envio da mensagem
        msg = canMessage(1026, false, 8); 
        msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
        try, transmit(ch, msg); catch, end
        
        fprintf('ENVIANDO (Hex): %02X %02X (Bits 1 = Desligado)\n', b1, b2);
        disp('----------------------------------------');
        
        % --- EXIBIÇÃO VISUAL (Mantém lógica normal para o usuário) ---
        for i = 1:8
            st = '[ OFF ]'; 
            if reles(i), st = '[ ON  ] ATIVADO'; end
            
            nome = sprintf('Saída D%d', i);
            if i==5, nome='NA2/NF2'; end
            if i==6, nome='NA1/NF1'; end
            if i==8, nome='Bomba'; end
            
            fprintf(' %d. %s -> %s\n', i, st, nome);
        end
        
        disp('----------------------------------------');
        disp(' 9. LIGAR TUDO'); disp('10. DESLIGAR TUDO'); disp(' x. VOLTAR');
        
        inp = input('Opção: ', 's');
        
        if strcmpi(inp, 'x')
            % AO SAIR: Mandar TUDO 1 para garantir que desligue
            % 64 + 16 + 4 + 1 = 85 (0x55)
            msg.Data = [85, 85, 0, 0, 0, 0, 0, 0]; 
            transmit(ch, msg); 
            sairTeste = true;
        else
            val = str2double(inp);
            if ~isnan(val)
                if val >= 1 && val <= 8
                    reles(val) = ~reles(val); % Inverte estado visual
                elseif val == 9
                    reles(:) = 1; % Visualmente tudo 1 (Envia 0)
                elseif val == 10
                    reles(:) = 0; % Visualmente tudo 0 (Envia 1)
                end
            end
        end
    end
end


%% --- FUNÇÃO 11: MONITORAR TÉRMICOS COM STATUS (OK/ERRO) ---
function monitorarModulosTermicos(ch)
    clc;
    disp('==================================================');
    disp('    MONITORAMENTO TÉRMICO + STATUS DOS SENSORES');
    disp('==================================================');
    
    % Configuração de Tempo
    delayStr = input('Tempo de atualização (segundos) [Padrão: 0.5]: ', 's');
    delayVal = str2double(delayStr);
    if isnan(delayVal) || delayVal < 0.1, delayVal = 0.5; end
    
    fprintf('>>> Iniciando (%.2fs)... Para SAIR, feche a janela.\n', delayVal);
    
    % Janela de Saída
    figKey = figure('Name','MONITOR TÉRMICO - Feche para Voltar','NumberTitle','off',...
                    'MenuBar','none','ToolBar','none',...
                    'Position',[100 100 550 100], 'Color', [0.8 0.8 0.8]); 
    
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA VOLTAR AO MENU',...
              'Position',[20 20 510 60], 'BackgroundColor',[0.8 0.8 0.8],...
              'FontSize', 12, 'FontWeight', 'bold');
              
    set(figKey, 'CurrentCharacter', char(0));
    
    % IDs e Textos de Status
    idsTermicos = [1296, 1312, 1328];
    nomesModulos = {'M1 (Main)', 'M2 (Aux)', 'M3 (Aux)'};
    statusTxt = {'OK ', 'Er1', 'Er2', 'Er3'}; % 0=OK, 1..3=Erros
    
    try
        while true
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x'
                disp('>>> Voltando ao Menu...'); break;
            end
            
            % Envia RTRs
            for k=1:length(idsTermicos)
                try, transmit(ch, canMessage(idsTermicos(k), false, 0, 'Remote', true)); catch, end
            end
            
            pause(0.1); 
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                
                clc;
                fprintf('=== LEITURAS (Atualizacao: %.2fs) ===\n', delayVal);
                % Cabeçalho mais largo para caber o Status
                fprintf('%-10s | CJ(C) | %-13s | %-13s | %-13s | %-13s\n', ...
                        'MODULO', 'TL(C°)', 'TR(C°)', 'BL(C°)', 'BR(C°)');
                disp('------------------------------------------------------------------------------------------');
                
                for k=1:3
                    targetID = idsTermicos(k);
                    idx = find([msgs.ID] == targetID & ~[msgs.Remote], 1, 'last');
                    
                    if ~isempty(idx)
                        d = uint16(msgs(idx).Data);
                        
                        % --- DECODIFICAÇÃO COMPLETA (VALOR + STATUS) ---
                        
                        % Cold Junction (Byte 0)
                        cj = double(int8(d(1)));
                        
                        % 1. STARTER (TL)
                        % Status: Byte 2 (Bits 0-1) | Temp: Byte 2(2-7) + Byte 3(0-5)
                        stTL  = bitand(d(2), 3); 
                        rawTL = bitshift(d(2), -2) + bitshift(bitand(d(3), 63), 6);
                        
                        % 2. ENGINE (TR)
                        % Status: Byte 3 (Bits 6-7) | Temp: Byte 4 + Byte 5(0-3)
                        stTR  = bitshift(d(3), -6);
                        rawTR = d(4) + bitshift(bitand(d(5), 15), 8);
                        
                        % 3. INTERCOOLER (BL)
                        % Status: Byte 5 (Bits 4-5) | Temp: Byte 5(6-7)+Byte 6+Byte 7(0-1)
                        stBL  = bitand(bitshift(d(5), -4), 3);
                        rawBL = bitshift(bitand(d(5), 192), -6) + bitshift(d(6), 2) + bitshift(bitand(d(7), 3), 10);
                        
                        % 4. WATER (BR)
                        % Status: Byte 7 (Bits 2-3) | Temp: Byte 7(4-7)+Byte 8
                        stBR  = bitand(bitshift(d(7), -2), 3);
                        rawBR = bitshift(bitand(d(7), 240), -4) + bitshift(d(8), 4);
                        
                        % Conversão (-2048 Offset)
                        valTL = double(rawTL) - 2048;
                        valTR = double(rawTR) - 2048;
                        valBL = double(rawBL) - 2048;
                        valBR = double(rawBR) - 2048;
                        
                        % Formatação com Status [Val + St]
                        % Ex: " 90.5 [OK ]"
                        txtTL = sprintf('%5.1f [%s]', valTL, statusTxt{stTL+1});
                        txtTR = sprintf('%5.1f [%s]', valTR, statusTxt{stTR+1});
                        txtBL = sprintf('%5.1f [%s]', valBL, statusTxt{stBL+1});
                        txtBR = sprintf('%5.1f [%s]', valBR, statusTxt{stBR+1});
                        
                        fprintf('%-10s | %5.1f | %-13s | %-13s | %-13s | %-13s\n', ...
                            nomesModulos{k}, cj, txtTL, txtTR, txtBL, txtBR);
                    else
                        fprintf('%-10s |  --   |      --       |      --       |      --       |      --      \n', nomesModulos{k});
                    end
                end
                disp('------------------------------------------------------------------------------------------');
                fprintf('(Legenda: [OK ]=Normal, [ErX]=Erro Sensor)\n');
                fprintf('Starter = TL1, Engine = TR1, Intercooler = BL1, Water = BR1\n ')
            end
            pause(delayVal);
        end
    catch ME
        disp(['Erro: ' ME.message]);
    end
    if isvalid(figKey), close(figKey); end
end