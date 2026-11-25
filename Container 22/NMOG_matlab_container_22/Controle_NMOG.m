%% SISTEMA DE CONTROLE ARC - PAINEL DE GERENCIAMENTO (EGT-4 FUELTECH)
% Funcionalidades:
% - Leitura passiva EGT-4 (Broadcast ID: 0x02400000)
% - Controle de Temperatura Independente e Histerese (0x403 a 0x406)
% - Configuração Geral de Aquisição (0x409)
% - Máquina de Estados e Relés Manuais (0x402)
clear; clc;
try stop(canChannelList); catch; end 

% --- CONFIGURAÇÕES GLOBAIS ---
HARDWARE_DEVICE = 'VN1630A 1';
HARDWARE_CHANNEL = 2; 
BAUD_RATE = 1000000;
EGT_ID = hex2dec('02400000'); % 37748736 em Decimal (Broadcast FuelTech)

% Variáveis de Estado
masterCh = [];
sistemaValidado = false;

% Loop do Menu Principal
opcao = -1;
while opcao ~= 0
    disp(' ');
    disp('==================================================');
    disp('       ARC - CONTROLE DE ARREFECIMENTO (EGT-4)    ');
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
    disp('8. TESTE MANUAL DE RELÉS (0x402)');
    disp('9. CONFIGURAR SEGURANÇA E HISTERESE (0x403-0x406)');
    disp('10. CONFIGURAR TAXA DE AQUISIÇÃO (0x409)');
    disp('11. MONITORAR TEMPERATURAS (EGT-4)'); 
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
            disp('>>> Verificando rede (Aguardando Broadcast & RTRs)...');
            if masterCh.MessagesAvailable > 0, receive(masterCh, masterCh.MessagesAvailable); end
            
            % Envia RTR apenas para os módulos de configuração
            idsCheckConfig = hex2dec({'403','404','405','406','409'});
            try
                for k=1:length(idsCheckConfig)
                    msg = canMessage(idsCheckConfig(k), false, 0); msg.Remote = true;
                    transmit(masterCh, msg); pause(0.02); 
                end
            catch, disp('AVISO: Falha no envio.'); end
            
            pause(1.5); % Tempo para receber o broadcast da FuelTech (EGT) e as RTRs
            
            if masterCh.MessagesAvailable > 0
                msgs = receive(masterCh, masterCh.MessagesAvailable);
                idsFound = unique([msgs.ID]);
                
                disp('RESUMO DA REDE:');
                checkID(idsFound, EGT_ID, 'EGT-4 FUELTECH (0x02400000)');
                checkID(idsFound, hex2dec('423'), 'ENGINE CONFIG (0x423)');
                checkID(idsFound, hex2dec('424'), 'HEAT EXCHANGE CONFIG (0x424)');
                checkID(idsFound, hex2dec('425'), 'STARTER CONFIG (0x425)');
                checkID(idsFound, hex2dec('426'), 'INTERCOOLER CONFIG (0x426)');
                checkID(idsFound, hex2dec('429'), 'AQUISITION CONFIG (0x429)');
                
                if ismember(EGT_ID, idsFound)
                    sistemaValidado = true; disp('-> EGT-4 DETECTADO. SISTEMA VALIDADO.');
                else
                    sistemaValidado = false; disp('-> AVISO: EGT-4 Ausente (Temperaturas offline).');
                end
            else
                disp('-> NENHUMA RESPOSTA RECEBIDA.');
            end
            
        case 3 % --- AUTOMÁTICO ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            disp(' ');
            useDefault = input('Usar padrões (90C/45C)? (s/n): ', 's');
            if lower(useDefault) == 'n'
                limMotor = input('Limite MOTOR (T2) (ex: 85): ');
                limAgua  = input('Limite ÁGUA (T4) (ex: 50): ');
            else, limMotor = 90; limAgua = 45; end
            executarControle(masterCh, limMotor, limAgua, EGT_ID); 
            
        case 4 % --- FILTRAGEM ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            filtragem_agua(masterCh, EGT_ID);
            
        case 5 % --- ARREFECIMENTO ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            arrefecimento_func(masterCh, EGT_ID);
           
        case 6 % --- DESCARTE ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            Descarte_func(masterCh, EGT_ID);
            
        case 7 % --- SNIFFER ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            disp('>>> SNIFFER ATIVO (Pressione "x" na janela para sair)...');
            figKey = figure('Name','Pressione X para Sair','NumberTitle','off','MenuBar','none','Position',[100 100 300 50]);
            set(figKey, 'CurrentCharacter', char(0));
            while true
                if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', break; end
                if masterCh.MessagesAvailable > 0
                    msgs = receive(masterCh, masterCh.MessagesAvailable);
                    for k=1:length(msgs)
                        fprintf('ID: 0x%08X | Ext: %d | Data: %s\n', msgs(k).ID, msgs(k).Extended, sprintf('%02X ', msgs(k).Data));
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
            
        case 11 % --- MONITORAR TÉRMICOS ---
            if isempty(masterCh), disp('ERRO: Sem Conexão.'); continue; end
            monitorarModulosTermicos(masterCh, EGT_ID);
            
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

%% --- FUNÇÃO 10: CONFIGURAR AQUISIÇÃO (0x409) ---
function configurarTaxaAquisicao(ch)
    disp('--------------------------------------------------');
    disp('    CONFIGURAÇÃO DE AQUISIÇÃO E CONTROLE (0x409)');
    disp('--------------------------------------------------');
    
    if ch.MessagesAvailable > 0, receive(ch, ch.MessagesAvailable); end
    
    try
        msgRTR = canMessage(1033, false, 0); % 0x409
        msgRTR.Remote = true; 
        transmit(ch, msgRTR);
    catch, disp('Erro ao enviar RTR.'); end
    
    pause(0.5); 
    
    data = [0, 100, 0, 0, 0, 0, 0, 0]; % Default se não ler
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for k=1:length(msgs)
            if ~msgs(k).Remote && msgs(k).ID == 1065 % 0x429
                data = msgs(k).Data;
            end
        end
    end
    
    currTimer = double(data(1))*256 + double(data(2));
    currAnalog = data(3); 
    currCont = bitshift(data(4), -2); 
    
    fprintf('\n   1. Taxa (Timer)..........: %d ms\n', currTimer);
    fprintf('   2. Leitura Analógica.....: (Val: %d)\n', currAnalog);
    fprintf('   3. Aquisição Contínua....: (Val: %d)\n', currCont);
    disp('--------------------------------------------------');
    
    if lower(input('Deseja reconfigurar? (s/n): ', 's')) == 's'
        newTimer  = input('   Novo Tempo (ms) [10-3000]: ');
        newAnalog = input('   Habilitar Analógico? (1/0): ');
        newCont   = input('   Habilitar Contínuo?  (1/0): ');
        
        newData = data;
        newData(1) = bitshift(newTimer, -8); 
        newData(2) = bitand(newTimer, 255);  
        newData(3) = newAnalog;
        newData(4) = bitshift(bitand(newCont, 1), 2);
        
        msgConf = canMessage(1033, false, 8); % 0x409
        msgConf.Data = newData;
        transmit(ch, msgConf);
        disp('>>> Configuração 0x409 enviada!');
    else
        disp('Nenhuma alteração realizada.');
    end
end

%% --- FUNÇÃO 9: CONFIGURAR LIMIARES E HISTERESE ---
function configurarLimiaresPlaca(ch)
    disp('--------------------------------------------------');
    disp('    CONFIGURAÇÃO DE SEGURANÇA E HISTERESE');
    disp('    (1 = Habilitado/ON, 0 = Desabilitado/OFF)');
    disp('--------------------------------------------------');
    
    if ch.MessagesAvailable > 0, receive(ch, ch.MessagesAvailable); end
    
    % Envia RTR para os 4 módulos independentes
    idsReq = [1029, 1027, 1030, 1028]; % 0x405 (T1), 0x403 (T2), 0x406 (T3), 0x404 (T4)
    idsResp = [1061, 1059, 1062, 1060]; % 0x425, 0x423, 0x426, 0x424
    nomes = {'STARTER (T1)', 'ENGINE (T2)', 'INTERCOOLER (T3)', 'WATER/HEAT (T4)'};
    
    try
        for k=1:4, msg = canMessage(idsReq(k),false,0); msg.Remote=true; transmit(ch,msg); pause(0.05); end
    catch, disp('Erro ao enviar RTR.'); end
    
    pause(0.5);
    
    % Matriz para armazenar as 4 configurações (8 bytes cada)
    configs = zeros(4, 8); 
    
    if ch.MessagesAvailable > 0
        msgs = receive(ch, ch.MessagesAvailable);
        for i=1:length(msgs)
            if ~msgs(i).Remote
                for k=1:4
                    if msgs(i).ID == idsResp(k), configs(k, :) = msgs(i).Data; end
                end
            end
        end
    end
    
    % Exibição do Status Atual
    for k=1:4
        d = configs(k, :);
        en = bitand(d(2), 3); % Isola os 2 últimos bits do byte 1
        st = 'OFF'; if en == 1, st = 'ON '; end
        
        maxT = d(4);
        maxTm = double(d(5)) * 100;
        lowT = d(6);
        lowTm = double(d(7)) * 100;
        
        fprintf(' %d. %-15s: [%s] | Alta: %d C (%d ms) | Baixa: %d C (%d ms)\n', ...
            k, nomes{k}, st, maxT, maxTm, lowT, lowTm);
    end
    disp('--------------------------------------------------');
    
    if lower(input('Alterar alguma configuração? (s/n): ', 's')) == 's'
        modIdx = input(' Qual módulo deseja alterar? (1 a 4): ');
        if modIdx >= 1 && modIdx <= 4
            disp(['--- Configurando ' nomes{modIdx} ' ---']);
            nEn  = input('   Habilitar Monitoramento? (1=ON, 0=OFF): ');
            nMax = input('   Temp Alta (Disparo) [C]: ');
            nTMax = input('   Tempo Alta [ms]: ');
            nLow = input('   Temp Baixa (Normaliza) [C]: ');
            nTLow = input('   Tempo Baixa [ms]: ');
            
            % Monta payload (Mantendo compatibilidade com C)
            newData = zeros(1, 8, 'uint8');
            newData(1) = hex2dec('12');
            newData(2) = bitshift(3, 4) + bitand(nEn, 3); % 0x30 | Enable
            newData(3) = 0;
            newData(4) = nMax;
            newData(5) = nTMax / 100;
            newData(6) = nLow;
            newData(7) = nTLow / 100;
            newData(8) = 0;
            
            msgConf = canMessage(idsReq(modIdx), false, 8);
            msgConf.Data = newData;
            transmit(ch, msgConf);
            fprintf('>>> Configuração enviada para %s!\n', nomes{modIdx});
        else
            disp('Módulo inválido.');
        end
    else
        disp('Nenhuma alteração realizada.');
    end
end



%% --- MÁQUINAS DE ESTADO E MONITORAMENTO (LENDO EGT-4) ---
function executarControle(ch, limM, limA, egtID)
    disp('>>> CONTROLE AUTOMÁTICO INICIADO');
    figKey = figure('Name','CONTROLE ATIVO - Feche para Voltar','NumberTitle','off',...
                    'MenuBar','none','ToolBar','none','Position',[100 100 400 100], ...
                    'Color', [0.9 0.4 0.4]); 
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA VOLTAR',...
              'Position',[20 20 360 60], 'BackgroundColor',[0.9 0.4 0.4], ...
              'FontSize', 12, 'FontWeight', 'bold', 'ForegroundColor', 'white');
    set(figKey, 'CurrentCharacter', char(0));
    cleanupObj = onCleanup(@() desligarSeguro(ch));
    
    estado = 2; 
    lastLog = clock; 
    startTime = now;
    histM = limM - 3;
    histA = limA - 5;
    
    % Inicializa temperaturas e textos
    tempENG = 0; tempHex = 0; tempINT = 0; TempSTART = 0;
    txt1 = ' -- [ERRO]';
    txt2 = ' -- [ERRO]';
    txt3 = ' -- [ERRO]';
    txt4 = ' -- [ERRO]';
    
    try
        while true
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x'
                break; 
            end
            
            tNow = (now - startTime) * 86400;
            
            % --- LEITURA EGT-4 (PASSIVA) ---
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                idx = find([msgs.ID] == egtID, 1, 'last');
                if ~isempty(idx)
                    d = msgs(idx).Data;
                    
                    rawT1 = typecast(uint8([d(2) d(1)]), 'int16'); 
                    rawT2 = typecast(uint8([d(4) d(3)]), 'int16');
                    rawT3 = typecast(uint8([d(6) d(5)]), 'int16');
                    rawT4 = typecast(uint8([d(8) d(7)]), 'int16');
                    
                    % Detecta erro: 8400 raw = 1050°C
                    erroEGT = false;
                    
                    if rawT1 == 8400
                        tempENG = 1050;
                        txt1 = ' -- [ERRO EGT]';
                        erroEGT = true;
                    else
                        tempENG = double(rawT1) * 0.125;
                        txt1 = sprintf('%6.1f C', tempENG);
                    end
                    
                    if rawT2 == 8400
                        tempHex = 1050;
                        txt2 = ' -- [ERRO EGT]';
                        erroEGT = true;
                    else
                        tempHex = double(rawT2) * 0.125;
                        txt2 = sprintf('%6.1f C', tempHex);
                    end
                    
                    if rawT3 == 8400
                        tempINT = 1050;
                        txt3 = ' -- [ERRO EGT]';
                        erroEGT = true;
                    else
                        tempINT = double(rawT3) * 0.125;
                        txt3 = sprintf('%6.1f C', tempINT);
                    end
                    
                    if rawT4 == 8400
                        TempSTART = 1050;
                        txt4 = ' -- [ERRO EGT]';
                        erroEGT = true;
                    else
                        TempSTART = double(rawT4) * 0.125;
                        txt4 = sprintf('%6.1f C', TempSTART);
                    end
                    
                    % Se algum canal estiver em erro (1050°C), não faz nada
                    if erroEGT
                        fprintf('\n*** CANAL EGT COM ERRO (1050°C) - NENHUMA SAÍDA ACIONADA ***\n');
                        % Mantém bomba, v1, v2 = 0
                        bomba = 0; v1 = 0; v2 = 0;
                    else
                        % --- LÓGICA DE ESTADOS (só roda se EGT OK) ---
                        bomba = 0; v1 = 0; v2 = 0; d1 = 0; d2 = 0;
                        switch estado
                            case 1 % S1
                                if tempENG > 0
                                    estado = 2; 
                                end
                            case 2 % S2
                                bomba = 1;
                                if tempENG >= limM
                                    estado = 3; 
                                end
                            case 3 % S3
                                bomba = 1; 
                                v1 = 1; 
                                if tempHex >= limA
                                    estado = 4; 
                                end
                                if tempENG < histM
                                    estado = 2; 
                                end
                            case 4 % S4
                                bomba = 1; 
                                v1 = 1; 
                                v2 = 1;
                                if tempENG < histM && tempHex < histA
                                    estado = 3; 
                                end
                        end
                        
                        % Alarme / descarte se superaquecimento crítico
                        if tempENG > (limM + 15)
                            d1 = 1; 
                            d2 = 1; 
                        end
                    end
                end
            end
            
            % --- ENVIO DE RELÉS (0x402 / 1026) ---
            if etime(clock, lastLog) > 0.5
                % Monta vetor "reles" conforme estado desejado
                reles = zeros(1,8);
                reles(3) = v2;
                reles(4) = v1;
                reles(5) = bomba;
                % d1/d2 podem ser usados em D1/D2 se desejar
                
                % Constrói b1 e b2 exatamente como no teste de relés
                b1 = 0;
                if reles(1) == 0, b1 = bitor(b1, 64); end  % D1
                if reles(2) == 0, b1 = bitor(b1, 16); end  % D2
                if reles(3) == 0, b1 = bitor(b1, 4);  end  % D3 (NA2/NF2)
                if reles(4) == 0, b1 = bitor(b1, 1);  end  % D4 (NA1/NF1)
                
                b2 = 0;
                if reles(5) == 0, b2 = bitor(b2, 64); end  % D5 (Bomba)
                if reles(6) == 0, b2 = bitor(b2, 16); end  % D6
                if reles(7) == 0, b2 = bitor(b2, 4);  end  % D7
                if reles(8) == 0, b2 = bitor(b2, 1);  end  % D8
                
                try
                    msg = canMessage(1026, false, 8);
                    msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
                    transmit(ch, msg);
                    
                    if erroEGT
                        fprintf('ENVIANDO CAN: b1=%02X b2=%02X | ERRO EGT - SAÍDAS DESLIGADAS\n', b1, b2);
                    else
                        fprintf('ENVIANDO CAN: b1=%02X b2=%02X | Estado=%d | Bomba=%d V1=%d V2=%d\n', ...
                                b1, b2, estado, bomba, v1, v2);
                    end
                catch ME
                    fprintf('ERRO ao transmitir CAN: %s\n', ME.message);
                end
                
                clc; 
                if erroEGT
                    fprintf('CONTROLE | ERRO EGT (1050°C) - AGUARDANDO SINAL VÁLIDO\n');
                    fprintf('           | T1:%s | T2:%s | T3:%s | T4:%s\n', txt1, txt2, txt3, txt4);
                else
                    fprintf('CONTROLE | T:%6.1fs | Motor(T1):%s | Hex(T2):%s | INT(T3):%s | Start(T4):%s\n', ...
                            tNow, txt1, txt2, txt3, txt4);
                    fprintf('           | Estado=%d | Bomba=%d V1=%d V2=%d\n', estado, bomba, v1, v2);
                end
                
                lastLog = clock;
            end
            
            pause(0.02);
        end
    catch ME
        disp(['Erro em executarControle: ' ME.message]);
    end
    
    % --- DESLIGAR TUDO AO SAIR ---
    try
        msgSair = canMessage(1026, false, 8);
        msgSair.Data = [85, 85, 0, 0, 0, 0, 0, 0];  % todas OFF
        transmit(ch, msgSair);
        fprintf('CAN: todas as saídas desligadas (b1=55, b2=55)\n');
    catch ME
        fprintf('ERRO ao desligar saídas: %s\n', ME.message);
    end
    
    if isvalid(figKey)
        close(figKey); 
    end
end

%% Função Filtragem
function filtragem_agua(ch, egtID)
    figKey = figure('Name','FILTRAGEM','NumberTitle','off','MenuBar','none', ...
                    'Position',[100 100 400 100], 'Color', [0.4 0.4 0.9]); 
    uicontrol('Style','text', 'String', 'FECHE PARA PARAR (BOMBA ON)', ...
              'Position',[20 20 360 60], 'BackgroundColor',[0.4 0.4 0.9], ...
              'ForegroundColor', 'white');
    set(figKey, 'CurrentCharacter', char(0));
    
    % onCleanup já chama desligarSeguro(ch) quando a função terminar
    cleanupObj = onCleanup(@() desligarSeguro(ch));
    
    lastLog = clock; 
    startTime = now;
    
    % Inicializa textos
    txt1 = ' -- [ERRO]';
    txt2 = ' -- [ERRO]';
    txt3 = ' -- [ERRO]';
    txt4 = ' -- [ERRO]';
    
    % --- ESTADO DESEJADO: só bomba (D5) ON, resto OFF ---
    reles = zeros(1,8);  % todas OFF
    reles(5) = 1;        % bomba ON
    
    try
        while true
            % Verifica se a figura foi fechada ou se recebeu 'x'
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x'
                break; 
            end
            
            % Lê mensagens CAN de temperatura
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                idx = find([msgs.ID] == egtID, 1, 'last');
                if ~isempty(idx)
                    d = msgs(idx).Data;
                    r1 = typecast(uint8([d(2) d(1)]), 'int16');
                    r2 = typecast(uint8([d(4) d(3)]), 'int16');
                    r3 = typecast(uint8([d(6) d(5)]), 'int16');
                    r4 = typecast(uint8([d(8) d(7)]), 'int16');
                    
                    t1 = double(r1) * 0.125;
                    t2 = double(r2) * 0.125;
                    t3 = double(r3) * 0.125;
                    t4 = double(r4) * 0.125;
                    
                    % 8400 (bruto) = 1050°C, representa erro/desconectado
                    txt1 = sprintf('%6.1f C', t1); if r1 == 8400, txt1 = ' -- [ERRO]'; end % Engine
                    txt2 = sprintf('%6.1f C', t2); if r2 == 8400, txt2 = ' -- [ERRO]'; end % HEx
                    txt3 = sprintf('%6.1f C', t3); if r3 == 8400, txt3 = ' -- [ERRO]'; end % INT
                    txt4 = sprintf('%6.1f C', t4); if r4 == 8400, txt4 = ' -- [ERRO]'; end % STARTER
                end
            end
            
            % A cada 0,5 s, envia comando CAN com SOMENTE a bomba ligada
            if etime(clock, lastLog) > 0.5
                % --- EXATAMENTE COMO NO TESTE DE RELÉS ---
                b1 = 0;
                if reles(1) == 0, b1 = bitor(b1, 64); end 
                if reles(2) == 0, b1 = bitor(b1, 16); end
                if reles(3) == 0, b1 = bitor(b1, 4);  end
                if reles(4) == 0, b1 = bitor(b1, 1);  end 
                
                b2 = 0;
                if reles(5) == 0, b2 = bitor(b2, 64); end 
                if reles(6) == 0, b2 = bitor(b2, 16); end
                if reles(7) == 0, b2 = bitor(b2, 4);  end
                if reles(8) == 0, b2 = bitor(b2, 1);  end
                
                % Envia mensagem CAN
                try
                    msg = canMessage(1026, false, 8);
                    msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
                    transmit(ch, msg);
                    
                    fprintf('ENVIANDO CAN: b1=%02X b2=%02X (Bomba ON)\n', b1, b2);
                catch ME
                    fprintf('ERRO ao transmitir CAN: %s\n', ME.message);
                end
                
                clc; 
                fprintf('FILTRAGEM | T:%6.1fs | Motor(T1):%s | Hex(T2):%s | INT(T3):%s | Start(T4):%s | BOMBA ON\n', ...
                        (now-startTime)*86400, txt1, txt2, txt3, txt4);
                lastLog = clock;
            end
            
            pause(0.02);
        end
    catch ME
        fprintf('ERRO geral em filtragem_agua: %s\n', ME.message);
    end
    
    % --- DESLIGAR TUDO AO SAIR (BOMBA OFF) ---
    try
        msgSair = canMessage(1026, false, 8);
        msgSair.Data = [85, 85, 0, 0, 0, 0, 0, 0];  % todas OFF
        transmit(ch, msgSair);
        fprintf('CAN: todas as saídas desligadas (b1=55, b2=55)\n');
    catch ME
        fprintf('ERRO ao desligar saídas: %s\n', ME.message);
    end
    
    if isvalid(figKey)
        close(figKey); 
    end
end
%% Função arrefecimento
function arrefecimento_func(ch, egtID)
    figKey = figure('Name','ARREFECIMENTO','NumberTitle','off','MenuBar','none', ...
                    'Position',[100 100 400 100], 'Color', [0.4 0.8 0.4]); 
    uicontrol('Style','text', 'String', 'FECHE PARA PARAR (BOMBA+NA1/NF1 ON)', ...
              'Position',[20 20 360 60], 'BackgroundColor',[0.4 0.8 0.4], ...
              'ForegroundColor', 'black');
    set(figKey, 'CurrentCharacter', char(0));
    cleanupObj = onCleanup(@() desligarSeguro(ch));
    
    lastLog = clock; 
    startTime = now;
    
    % Inicializa textos
    txt1 = ' -- [ERRO]';
    txt2 = ' -- [ERRO]';
    txt3 = ' -- [ERRO]';
    txt4 = ' -- [ERRO]';
    
    % --- ESTADO DESEJADO: Bomba (D5) e NA1/NF1 (D4) ON, resto OFF ---
    % reles(i) = 1 -> ON (bit 0 no CAN)
    % reles(i) = 0 -> OFF (bit 1 no CAN)
    reles = zeros(1,8);  % todas OFF
    reles(4) = 1;        % NA1/NF1 ON
    reles(5) = 1;        % Bomba ON
    
    try
        while true
            % Verifica se a figura foi fechada ou se recebeu 'x'
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x'
                break; 
            end
            
            % Lê mensagens CAN de temperatura
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                idx = find([msgs.ID] == egtID, 1, 'last');
                if ~isempty(idx)
                    d = msgs(idx).Data;
                    r1 = typecast(uint8([d(2) d(1)]), 'int16');
                    r2 = typecast(uint8([d(4) d(3)]), 'int16');
                    r3 = typecast(uint8([d(6) d(5)]), 'int16');
                    r4 = typecast(uint8([d(8) d(7)]), 'int16');
                    
                    t1 = double(r1) * 0.125;
                    t2 = double(r2) * 0.125;
                    t3 = double(r3) * 0.125;
                    t4 = double(r4) * 0.125;
                    
                    % 8400 (bruto) = 1050°C, representa erro/desconectado
                    txt1 = sprintf('%6.1f C', t1); if r1 == 8400, txt1 = ' -- [ERRO]'; end % Engine
                    txt2 = sprintf('%6.1f C', t2); if r2 == 8400, txt2 = ' -- [ERRO]'; end % HEx
                    txt3 = sprintf('%6.1f C', t3); if r3 == 8400, txt3 = ' -- [ERRO]'; end % INT
                    txt4 = sprintf('%6.1f C', t4); if r4 == 8400, txt4 = ' -- [ERRO]'; end % STARTER
                end
            end
            
            % A cada 0,5 s, envia comando CAN com Bomba + NA1/NF1 ON
            if etime(clock, lastLog) > 0.5
                % --- MESMA LÓGICA DO TESTE DE RELÉS ---
                b1 = 0;
                if reles(1) == 0, b1 = bitor(b1, 64); end  % D1 OFF
                if reles(2) == 0, b1 = bitor(b1, 16); end  % D2 OFF
                if reles(3) == 0, b1 = bitor(b1, 4);  end  % D3 OFF
                if reles(4) == 0, b1 = bitor(b1, 1);  end  % D4 (NA1/NF1)
                
                b2 = 0;
                if reles(5) == 0, b2 = bitor(b2, 64); end  % D5 (Bomba)
                if reles(6) == 0, b2 = bitor(b2, 16); end  % D6 OFF
                if reles(7) == 0, b2 = bitor(b2, 4);  end  % D7 OFF
                if reles(8) == 0, b2 = bitor(b2, 1);  end  % D8 OFF
                
                % Com reles(4)=1 e reles(5)=1:
                % b1 = 64+16+4 = 84 (0x54) -> D4 bit 0 = 0 (ON)
                % b2 = 16+4+1 = 21 (0x15) -> D5 bit 6 = 0 (ON)
                
                try
                    msg = canMessage(1026, false, 8);
                    msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
                    transmit(ch, msg);
                    
                    fprintf('ENVIANDO CAN: b1=%02X b2=%02X (Bomba + NA1/NF1 ON)\n', b1, b2);
                catch ME
                    fprintf('ERRO ao transmitir CAN: %s\n', ME.message);
                end
                
                clc; 
                fprintf('ARREFECIM | T:%6.1fs | Motor(T1):%s | Hex(T2):%s | INT(T3):%s | Start(T4):%s | BOMBA + NA1/NF1 ON\n', ...
                        (now-startTime)*86400, txt1, txt2, txt3, txt4);
                lastLog = clock;
            end
            
            pause(0.02);
        end
    catch ME
        fprintf('ERRO geral em arrefecimento_func: %s\n', ME.message);
    end
    
    % --- DESLIGAR TUDO AO SAIR ---
    try
        msgSair = canMessage(1026, false, 8);
        msgSair.Data = [85, 85, 0, 0, 0, 0, 0, 0];  % todas OFF
        transmit(ch, msgSair);
        fprintf('CAN: todas as saídas desligadas (b1=55, b2=55)\n');
    catch ME
        fprintf('ERRO ao desligar saídas: %s\n', ME.message);
    end
    
    if isvalid(figKey)
        close(figKey); 
    end
end
%% Função descarte
function Descarte_func(ch, egtID)
    figKey = figure('Name','DESCARTE','NumberTitle','off','MenuBar','none', ...
                    'Position',[100 100 400 100], 'Color', [0.9 0.6 0.2]); 
    uicontrol('Style','text', 'String', 'FECHE PARA PARAR (BOMBA+V1+V2 ON)', ...
              'Position',[20 20 360 60], 'BackgroundColor',[0.9 0.6 0.2], ...
              'ForegroundColor', 'black');
    set(figKey, 'CurrentCharacter', char(0));
    cleanupObj = onCleanup(@() desligarSeguro(ch));
    
    lastLog = clock; 
    startTime = now;
    
    % Inicializa textos
    txt1 = ' -- [ERRO]';
    txt2 = ' -- [ERRO]';
    txt3 = ' -- [ERRO]';
    txt4 = ' -- [ERRO]';
    
    % --- ESTADO DESEJADO: Bomba (D5), NA1/NF1 (D4) e NA2/NF2 (D3) ON, resto OFF ---
    % reles(i) = 1 -> ON (bit 0 no CAN)
    % reles(i) = 0 -> OFF (bit 1 no CAN)
    reles = zeros(1,8);  % todas OFF
    reles(3) = 1;        % NA2/NF2 ON
    reles(4) = 1;        % NA1/NF1 ON
    reles(5) = 1;        % Bomba ON
    
    try
        while true
            % Verifica se a figura foi fechada ou se recebeu 'x'
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x'
                break; 
            end
            
            % Lê mensagens CAN de temperatura
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                idx = find([msgs.ID] == egtID, 1, 'last');
                if ~isempty(idx)
                    d = msgs(idx).Data;
                    r1 = typecast(uint8([d(2) d(1)]), 'int16');
                    r2 = typecast(uint8([d(4) d(3)]), 'int16');
                    r3 = typecast(uint8([d(6) d(5)]), 'int16');
                    r4 = typecast(uint8([d(8) d(7)]), 'int16');
                    
                    t1 = double(r1) * 0.125;
                    t2 = double(r2) * 0.125;
                    t3 = double(r3) * 0.125;
                    t4 = double(r4) * 0.125;
                    
                    % 8400 (bruto) = 1050°C, representa erro/desconectado
                    txt1 = sprintf('%6.1f C', t1); if r1 == 8400, txt1 = ' -- [ERRO]'; end % Engine
                    txt2 = sprintf('%6.1f C', t2); if r2 == 8400, txt2 = ' -- [ERRO]'; end % HEx
                    txt3 = sprintf('%6.1f C', t3); if r3 == 8400, txt3 = ' -- [ERRO]'; end % INT
                    txt4 = sprintf('%6.1f C', t4); if r4 == 8400, txt4 = ' -- [ERRO]'; end % STARTER
                end
            end
            
            % A cada 0,5 s, envia comando CAN com Bomba + NA1/NF1 + NA2/NF2 ON
            if etime(clock, lastLog) > 0.5
                % --- MESMA LÓGICA DO TESTE DE RELÉS ---
                b1 = 0;
                if reles(1) == 0, b1 = bitor(b1, 64); end  % D1 OFF
                if reles(2) == 0, b1 = bitor(b1, 16); end  % D2 OFF
                if reles(3) == 0, b1 = bitor(b1, 4);  end  % D3 (NA2/NF2)
                if reles(4) == 0, b1 = bitor(b1, 1);  end  % D4 (NA1/NF1)
                
                b2 = 0;
                if reles(5) == 0, b2 = bitor(b2, 64); end  % D5 (Bomba)
                if reles(6) == 0, b2 = bitor(b2, 16); end  % D6 OFF
                if reles(7) == 0, b2 = bitor(b2, 4);  end  % D7 OFF
                if reles(8) == 0, b2 = bitor(b2, 1);  end  % D8 OFF
                
                % Com reles(3)=1, reles(4)=1, reles(5)=1:
                % b1 = 64+16 = 80 (0x50) -> D3 e D4 ON, D1 e D2 OFF
                % b2 = 16+4+1 = 21 (0x15) -> D5 ON, D6-D8 OFF
                
                try
                    msg = canMessage(1026, false, 8);
                    msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
                    transmit(ch, msg);
                    
                    fprintf('ENVIANDO CAN: b1=%02X b2=%02X (Bomba + NA1/NF1 + NA2/NF2 ON)\n', b1, b2);
                catch ME
                    fprintf('ERRO ao transmitir CAN: %s\n', ME.message);
                end
                
                clc; 
                fprintf('DESCARTE | T:%6.1fs | Motor(T1):%s | Hex(T2):%s | INT(T3):%s | Start(T4):%s | BOMBA + NA1/NF1 + NA2/NF2 ON\n', ...
                        (now-startTime)*86400, txt1, txt2, txt3, txt4);
                lastLog = clock;
            end
            
            pause(0.02);
        end
    catch ME
        fprintf('ERRO geral em Descarte_func: %s\n', ME.message);
    end
    
    % --- DESLIGAR TUDO AO SAIR ---
    try
        msgSair = canMessage(1026, false, 8);
        msgSair.Data = [85, 85, 0, 0, 0, 0, 0, 0];  % todas OFF
        transmit(ch, msgSair);
        fprintf('CAN: todas as saídas desligadas (b1=55, b2=55)\n');
    catch ME
        fprintf('ERRO ao desligar saídas: %s\n', ME.message);
    end
    
    if isvalid(figKey)
        close(figKey); 
    end
end
%% Desligar 
function desligarSeguro(ch)
    if ~isempty(ch) && isvalid(ch) && ch.Running
        try
            msg = canMessage(1026, false, 8, [85 85 0 0 0 0 0 0]);
            transmit(ch, msg); pause(0.05);
            fprintf('\n>>> HARDWARE DESLIGADO.\n');
        catch, end
    end
end

%% --- FUNÇÃO 8: TESTE MANUAL DE RELÉS ---
%% --- TESTE DE RELÉS ---
function executarTesteReles(ch)
    reles = zeros(1, 8); sairTeste = false;
    while ~sairTeste
        clc; disp('=== TESTE DE RELÉS (ID 0x402) - LÓGICA INVERSA ===');
        disp(' Nota: Enviando 0 para LIGAR e 1 para DESLIGAR o hardware');
        b1 = 0;
        if reles(1) == 0, b1 = bitor(b1, 64); end 
        if reles(2) == 0, b1 = bitor(b1, 16); end
        if reles(3) == 0, b1 = bitor(b1, 4);  end
        if reles(4) == 0, b1 = bitor(b1, 1);  end 
        b2 = 0;
        if reles(5) == 0, b2 = bitor(b2, 64); end 
        if reles(6) == 0, b2 = bitor(b2, 16); end 
        if reles(7) == 0, b2 = bitor(b2, 4);  end 
        if reles(8) == 0, b2 = bitor(b2, 1);  end 
        
        % CORREÇÃO AQUI: Cria e depois atribui os dados
        try
            msg = canMessage(1026, false, 8);
            msg.Data = [b1, b2, 0, 0, 0, 0, 0, 0];
            transmit(ch, msg); 
        catch
        end
        
        fprintf('ENVIANDO (Hex): %02X %02X (Bits 1 = Desligado)\n', b1, b2);
        disp('----------------------------------------');
        for i = 1:8
            st = '[ OFF ]'; if reles(i), st = '[ ON  ] ATIVADO'; end
            nome = sprintf('Saída D%d', i);
            if i==5, nome='Bomba'; end
            if i==4, nome='NA1/NF1'; end
            if i==3, nome='NA2/NF2'; end
            fprintf(' %d. %s -> %s\n', i, st, nome);
        end
        disp('----------------------------------------');
        disp(' 9. LIGAR TUDO'); disp('10. DESLIGAR TUDO'); disp(' x. VOLTAR');
        inp = input('Opção: ', 's');
        if strcmpi(inp, 'x')
            % CORREÇÃO AQUI NO BOTÃO DE SAÍDA
            msgSair = canMessage(1026, false, 8);
            msgSair.Data = [85, 85, 0, 0, 0, 0, 0, 0];
            transmit(ch, msgSair); 
            sairTeste = true;
        else
            val = str2double(inp);
            if ~isnan(val)
                if val >= 1 && val <= 8
                    reles(val) = ~reles(val); 
                elseif val == 9
                    reles(:) = 1; 
                elseif val == 10
                    reles(:) = 0; 
                end
            end
        end
    end
end

%% --- FUNÇÃO 11: MONITORAR TÉRMICOS (EGT-4) ---
function monitorarModulosTermicos(ch, egtID)
    figKey = figure('Name','MONITOR EGT-4','NumberTitle','off','MenuBar','none','Position',[100 100 550 100], 'Color', [0.8 0.8 0.8]); 
    uicontrol('Style','text', 'String', 'FECHE ESTA JANELA (X) PARA VOLTAR', 'Position',[20 20 510 60], 'BackgroundColor',[0.8 0.8 0.8], 'FontSize', 12, 'FontWeight', 'bold');
    set(figKey, 'CurrentCharacter', char(0));
    
    delayVal = 0.5;
    try
        while true
            if ~isvalid(figKey) || lower(get(figKey, 'CurrentCharacter')) == 'x', break; end
            
            % Não precisamos mais de RTR. A EGT-4 manda por conta própria.
            pause(delayVal); 
            
            if ch.MessagesAvailable > 0
                msgs = receive(ch, ch.MessagesAvailable);
                idx = find([msgs.ID] == egtID, 1, 'last');
                
                clc;
                disp('========================================================================');
                fprintf('    MONITORAMENTO FUELTECH EGT-4 (Broadcast)\n');
                disp('========================================================================');
                
                if ~isempty(idx)
                    d = msgs(idx).Data;
                    
                    % Extração Big-Endian convertendo de uint8 para int16 (Complemento de 2)
                    r1 = typecast(uint8([d(2) d(1)]), 'int16');
                    r2 = typecast(uint8([d(4) d(3)]), 'int16');
                    r3 = typecast(uint8([d(6) d(5)]), 'int16');
                    r4 = typecast(uint8([d(8) d(7)]), 'int16');
                    
                    t1 = double(r1) * 0.125;
                    t2 = double(r2) * 0.125;
                    t3 = double(r3) * 0.125;
                    t4 = double(r4) * 0.125;
                    
                    % 8400 (bruto) = 1050°C, representa erro/desconectado segundo o C
                    txt1 = sprintf('%6.1f C', t1); if r1 == 8400, txt1 = ' -- [ERRO]'; end
                    txt2 = sprintf('%6.1f C', t2); if r2 == 8400, txt2 = ' -- [ERRO]'; end
                    txt3 = sprintf('%6.1f C', t3); if r3 == 8400, txt3 = ' -- [ERRO]'; end
                    txt4 = sprintf('%6.1f C', t4); if r4 == 8400, txt4 = ' -- [ERRO]'; end
                    
                    fprintf('  > ENG. TEMP. (T1)......: %s\n', txt1);
                    fprintf('  > HEX. TEMP. (T2).......: %s\n', txt2);
                    fprintf('  > INT. TEMP. (T3)..: %s\n', txt3);
                    fprintf('  > START. TEMP. (T4)...: %s\n', txt4);
                else
                    disp('  Aguardando dados da EGT-4... (Sem sinal no barramento)');
                end
                disp('------------------------------------------------------------------------');
                fprintf('\n(Pressione X na janela cinza para sair)\n');
            end
        end
    catch ME, disp(['Erro: ' ME.message]); end
    if isvalid(figKey), close(figKey); end
end