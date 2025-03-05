#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MFRC522.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP_Mail_Client.h>



// **Definições de Pinos**
#define BUILTIN_LED_PIN 2
#define SS_PIN 5
#define RST_PIN 4
#define BUTTON_PIN 25
#define LED_PIN 32
#define RED_LED_PIN 26
#define BUZZER_PIN 33



// **Configuração do Servidor SMTP**
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT esp_mail_smtp_port_465
#define EMAIL_SENDER "fichadorelite17@gmail.com"
#define EMAIL_PASSWORD "xrmjhvekjssbkxoa"
#define EMAIL_RECIPIENT_1 "rdinis@elitesports17.com"
//#define EMAIL_RECIPIENT_2 "mmeissner@elitesports17.com"
//#define EMAIL_RECIPIENT_3 "adrimvalia@hotmail.com"


const char* ssid = "MOVISTAR-WIFI6-C118"; 
const char* wifiPassword = "eTXi9WN9ViYUP4HY4gbi"; 


const char* username = "rdinis@elitesports17.com";
const char* userPassword = "abc1234";


const char* loginEndpoint = "https://workdayapi.elitesports17.com/login.test"; 


const char* dataEndpoint = "https://workdayapi.elitesports17.com/user.sign.test"; 


String userId = "";
String tempToken = "";
String serverToken = "";
unsigned long serverTokenExpiry = 0; // Timestamp de expiração do server_token

// **Instâncias de Objetos**
MFRC522 mfrc522(SS_PIN, RST_PIN); // Instância do leitor RFID
LiquidCrystal_I2C lcd(0x27, 20, 4); // Endereço e tamanho do LCD
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); // Fuso horário +2 horas, atualização a cada 60 segundos

// **Tamanho do DynamicJsonDocument**
const size_t JSON_DOC_SIZE = 512; // Ajuste conforme a necessidade
DynamicJsonDocument doc(JSON_DOC_SIZE);

// **Variáveis de Controle**
int signMode = 1;
int lastButtonState = HIGH;
unsigned long lastButtonPressTime = 0;
const unsigned long inactivityTimeout = 10000;  
unsigned long bienvenidoDisplayTime = 0;
const unsigned long bienvenidoDisplayDuration = 2000;
unsigned long lastTokenRefreshTime = 0;
const unsigned long tokenRefreshInterval = 23UL * 60 * 60 * 1000; // 23 horas
// Variáveis para controle de renovação do token
bool tokenRenewedToday = false;  // Para verificar se o token já foi renovado hoje



const char* signModeNames[] = {"", "Entry", "Exit", "Start Break", "End Break"};




// Criar sessão SMTP
SMTPSession smtp;
ESP_Mail_Session session;
SMTP_Message message;

// **Função para exibir logs do envio de e-mail**
void smtpCallback(SMTP_Status status) {
    Serial.println(status.info());
    if (status.success()) {
        Serial.println("✅ E-mail enviado com sucesso!");
    } else {
        Serial.println("❌ Falha ao enviar e-mail.");
    }
}

// **Função de envio de e-mail em uma Task**
void sendEmailTask(void *parameter) {
    String fullname = ((String*)parameter)[0];
    String cardUID = ((String*)parameter)[1];
    delete[] (String*)parameter; // Liberar memória

    Serial.println("🔄 Configurando sessão SMTP...");
    session.server.host_name = SMTP_HOST;
    session.server.port = SMTP_PORT;
    session.login.email = EMAIL_SENDER;
    session.login.password = EMAIL_PASSWORD;
    session.login.user_domain = "127.0.0.1";
    session.time.ntp_server = "pool.ntp.org";
    session.time.gmt_offset = 0;
    session.time.day_light_offset = 0;

    smtp.callback(smtpCallback);

    message.sender.name = "Fichador RFID";
    message.sender.email = EMAIL_SENDER;
    message.subject = "⚠️ Atraso en la Entrada!";
    message.addRecipient("Creador", EMAIL_RECIPIENT_1);
    //message.addRecipient("Jefe", EMAIL_RECIPIENT_2);
    //message.addRecipient("Boss", EMAIL_RECIPIENT_3);
    
    String emailContent = "El empleado " + fullname + " (Tarjeta: " + cardUID + ") ha fichado despues de las 9:10h.";
    message.text.content = emailContent.c_str();
    message.text.charSet = "utf-8";
    message.text.transfer_encoding = Content_Transfer_Encoding::enc_base64;

    if (!smtp.connect(&session)) {
        Serial.println("❌ Falha ao conectar ao servidor SMTP: " + smtp.errorReason());
        vTaskDelete(NULL);
    }
    
    if (!MailClient.sendMail(&smtp, &message)) {
        Serial.println("❌ Falha ao enviar e-mail: " + smtp.errorReason());
    } else {
        Serial.println("✅ E-mail enviado com sucesso!");
    }

    smtp.closeSession();
    vTaskDelete(NULL);
}

// **Função para iniciar o envio do e-mail em segundo plano**
void triggerEmail(String fullname, String cardUID) {
    String* params = new String[2]{fullname, cardUID};
    xTaskCreatePinnedToCore(
        sendEmailTask,  // Função da task
        "EmailTask",   // Nome da task
        10000,          // Tamanho da stack
        params,         // Parâmetros passados
        1,              // Prioridade
        NULL,           // Handle da task
        0               // Executar na Core 0
    );
}

// **Função principal que detecta cartões e envia e-mail**
void detectCard(String fullname, String cardUID, int currentHour, int currentMinute) {
    Serial.println("Cartão autorizado, comunicação iniciada.");
    if (currentHour > 9 || (currentHour == 9 && currentMinute > 10)) {
        triggerEmail(fullname, cardUID); // Enviar e-mail em segundo plano
    }
}



// **Função para Conectar ao WiFi**
void setup_wifi() {
    delay(50);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, wifiPassword);
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
        retryCount++;
        if (retryCount > 10) { // Tempo máximo de espera: 10 segundos
            Serial.println("\nFailed to connect to WiFi. Restarting...");
            ESP.restart(); // Reinicia o ESP32 caso não consiga conectar
        }
    }
    Serial.println("");
    Serial.print("Connected to WiFi. IP Address: ");
    Serial.println(WiFi.localIP());
}

// **Função para Exibir Mensagem de Boas-Vindas**
void displayWelcomeMessage() {
    lcd.clear();
    lcd.setCursor(3, 1);
    lcd.print("McNaughtans");
    lcd.setCursor(3, 2);
    lcd.print("Welcome");

}

// **Função para Tratar Erros**
void handleError(int errorCode, const String& errorMsg) {
    digitalWrite(RED_LED_PIN, HIGH);
    lcd.clear();
    lcd.setCursor(0, 2);
    lcd.print("Error ");
    lcd.print(errorCode);
    lcd.setCursor(0, 3);
    lcd.print(errorMsg);  

    delay(300);  

    for (int i = 0; i < 3; i++) {
        tone(BUZZER_PIN, 2000, 200);
        delay(200);
        noTone(BUZZER_PIN);
        delay(200);
    }

    digitalWrite(RED_LED_PIN, LOW); 
}

// **Função para Autenticar com mode=username**

bool authenticateWithUsername() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi não está conectado. Tentando reconectar...");
        setup_wifi();
    }

    HTTPClient http;
    Serial.println("Autenticando com mode=username...");

    http.begin(loginEndpoint);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded"); // Alterado para form-urlencoded

    // Preparar os dados do POST para mode=username em form-urlencoded
    String postData = "mode=username&username=" + String(username);

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Resposta do servidor (username): " + response);

        // Analisar o JSON para obter o id e o token temporal
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.print("Erro ao analisar JSON de autenticação: ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }

        // Acessar os valores aninhados dentro de "result"
        JsonObject result = doc["result"];
        if (!result.isNull()) {
            userId = result["id"].as<String>();
            tempToken = result["token"].as<String>(); // Certifique-se de que o campo está correto

            if (userId == "" || tempToken == "") {
                Serial.println("Falha ao obter id ou token temporal.");
                http.end();
                return false;
            }

            Serial.println("Usuário autenticado. ID: " + userId + ", Token Temporal: " + tempToken);
            Serial.println("Token Temporal: " + tempToken); // Print do token temporal no terminal
            http.end();
            return true;
        } else {
            Serial.println("Erro: campo 'result' não encontrado na resposta JSON.");
            http.end();
            return false;
        }
    } else {
        Serial.print("Erro na solicitação de autenticação (username). Código de erro: ");
        Serial.println(httpResponseCode);
        http.end();
        return false;
    }
}


// **Função para Autenticar com mode=password**
bool authenticateWithPassword() {
    if (userId == "" || tempToken == "") {
        Serial.println("ID do usuário ou token temporal não estão disponíveis. Autentique com username primeiro.");
        return false;
    }

    HTTPClient http;
    Serial.println("Autenticando com mode=password...");

    http.begin(loginEndpoint);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded"); // Alterado para form-urlencoded
    http.addHeader("Authorization", "Bearer " + tempToken); // Se necessário

    // Preparar os dados do POST para mode=password em form-urlencoded
    String postData = "mode=password&id=" + userId + "&password=" + String(userPassword);

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Resposta do servidor (password): " + response);

        // Analisar o JSON para obter o server token e o tempo de expiração
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.print("Erro ao analisar JSON do server token: ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }

// Verificar se o status é "success" ou "error"
        String status = doc["status"].as<String>();
          if (status != "ok") {
        String message = doc["message"].as<String>();
        Serial.println("Erro na autenticação (password): " + message);
        http.end();
        return false;
}

// Acessar o token dentro de "result"
        serverToken = doc["result"]["token"].as<String>();
        int expiresIn = doc["expires_in"].as<int>(); // Tempo em segundos (caso exista essa chave)

      if (serverToken == "") {
       Serial.println("Falha ao obter server token.");
       http.end();
       return false;
}

      Serial.println("Server Token obtido: " + serverToken);
      Serial.println("Server Token: " + serverToken); // Print do server token no terminal
      http.end();
      return true;

        
    } else {
        Serial.print("Erro na solicitação de autenticação (password). Código de erro: ");
        Serial.println(httpResponseCode);
        http.end();
        return false;
    }
}

// **Função de Login Completo: Autenticação de Duas Etapas**
bool performLogin() {
    // Etapa 1: Autenticação com mode=username
    if (!authenticateWithUsername()) {
        Serial.println("Falha na autenticação com mode=username.");
        return false;
    }

    // Etapa 2: Autenticação com mode=password
    if (!authenticateWithPassword()) {
        Serial.println("Falha na autenticação com mode=password.");
        return false;
    }

    // Atualizar o timestamp de renovação do token
    lastTokenRefreshTime = millis();
    return true;
}

// **Função para Garantir que o server_token está Válido**
bool ensureServerToken() {
    // Verificar se o server_token está vazio ou expirado
    if (serverToken == "" || millis() >= serverTokenExpiry) {
        Serial.println("server_token expirado ou não disponível. Realizando login novamente...");

        if (!performLogin()) {
            Serial.println("Falha ao realizar login para obter server_token.");
            return false;
        }
    }
    return true;
}

// **Função para Buscar Dados no Servidor Usando o server_token**
bool fetch_data(String cardUID, int signMode) {
    // Garantir que o server_token está válido
    if (!ensureServerToken()) {
        Serial.println("Não foi possível garantir um server_token válido.");
        handleError(-1, "Falha na autenticação");
        return false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.useHTTP10(true);
        Serial.print("\nFazendo requisição HTTP para a URL: ");
        Serial.println(dataEndpoint);
        http.begin(dataEndpoint);
        http.addHeader("Authorization", "Bearer " + serverToken);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded"); 

        String postData = "card_id=" + cardUID + "&sign_mode_id=" + String(signMode);
        
        //lcd.clear();
        //lcd.setCursor(0, 1);
        //lcd.print("Aguardando respuesta");

        // Início do temporizador
        unsigned long startTime = millis();

        int httpResponseCode = http.POST(postData);

        // Fim do temporizador
        unsigned long endTime = millis();
        unsigned long duration = endTime - startTime;
        Serial.print("Tempo da requisição HTTP (ms): ");
        Serial.println(duration);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("Resposta do servidor: " + response);

            // Deserializar o JSON para obter o status
            DeserializationError error = deserializeJson(doc, response);
            if (error) {
                Serial.print("Erro ao analisar JSON: ");
                Serial.println(error.c_str());
                return false;
            }

            // Checar o status retornado no JSON
            String status = doc["status"].as<String>();
            if (status == "error") {
                int errorCode = doc["result"]["error_id"].as<int>();
                String errorMsg;

                if (errorCode == 400) {
                    errorMsg = "Unavailable";
                } else if (errorCode == 401) {
                    errorMsg = "Card Error";
                } else if (errorCode == 500) {
                    errorMsg = "Server Error";
                } else {
                    errorMsg = "Unknown Error";
                }

                handleError(errorCode, errorMsg);
                lcd.clear();
                lcd.print(signModeNames[signMode]);

                return false;
            }

            String fullname = doc["result"]["fullname"].as<String>();
            String firstName = fullname.substring(0, fullname.indexOf(' '));
            digitalWrite(LED_PIN, HIGH);
            lcd.setCursor(0, 1);
            lcd.print("                    "); // Limpa a linha 1
            lcd.setCursor(0, 2);
            if (signMode == 1) {
                lcd.print("Bonjour " + firstName);
                triggerEmail(fullname, cardUID);
            } else if (signMode == 2) {
                lcd.print("Arrivederci " + firstName);
            } else if (signMode == 3) {
                lcd.print("Caproveche " + firstName);
            } else if (signMode == 4) {
                lcd.print("Hola " + firstName);
            }

            bienvenidoDisplayTime = millis();
            tone(BUZZER_PIN, 2000, 500);
            delay(1000);
            digitalWrite(LED_PIN, LOW);
            noTone(BUZZER_PIN);
            return true;

        } else {
            Serial.print("Erro na solicitação. Código de erro: ");
            Serial.println(httpResponseCode);
            return false;
        }

        http.end();
    } else if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi Desconectado");
        setup_wifi();
    }

    return false;
}


// **Função para Imprimir o Horário Local no LCD**
void printLocalTime() {
    timeClient.update();
    time_t epochTime = timeClient.getEpochTime();
    struct tm *timeinfo = localtime(&epochTime);
    if (timeinfo == NULL) {
        Serial.println("No time available (yet)");
        return;
    }
    lcd.setCursor(0, 0);
    lcd.print((timeinfo->tm_mday < 10) ? "0" : "");
    lcd.print(timeinfo->tm_mday);
    lcd.print("/");
    lcd.print((timeinfo->tm_mon + 1 < 10) ? "0" : "");
    lcd.print(timeinfo->tm_mon + 1);
    lcd.print("/");
    lcd.print(timeinfo->tm_year + 1900);
    lcd.setCursor(12, 0);
    lcd.print((timeinfo->tm_hour < 10) ? "0" : "");
    lcd.print(timeinfo->tm_hour);
    lcd.print(":");
    lcd.print((timeinfo->tm_min < 10) ? "0" : "");
    lcd.print(timeinfo->tm_min);
    lcd.print(":");
    lcd.print((timeinfo->tm_sec < 10) ? "0" : "");
    lcd.print(timeinfo->tm_sec);
    if (millis() - bienvenidoDisplayTime >= bienvenidoDisplayDuration) {
        lcd.setCursor(0, 2);
        lcd.print("                      ");  // Limpa apenas a linha 2
        lcd.setCursor(0, 3);
        lcd.print(signModeNames[signMode]);  // Reexibe o signMode
    }
}

// **Função de Configuração Inicial**
void setup() {
    Serial.begin(115200);
    pinMode(BUILTIN_LED_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    digitalWrite(BUILTIN_LED_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    
    lcd.init();
    lcd.backlight();
    lcd.clear();
    displayWelcomeMessage();
    //playMelody(silentNightMelody, silentNightTempo, sizeof(silentNightMelody) / sizeof(int));
    //playMelody(wishYouMelody, wishYouTempo, sizeof(wishYouMelody) / sizeof(int));
    
    setup_wifi();
    SPI.begin(); // Iniciar SPI para MFRC522
    mfrc522.PCD_Init();
    
    // Iniciar cliente NTP
    timeClient.begin();
    timeClient.setTimeOffset(3600); // Ajuste conforme o fuso horário (7200 segundos = +2 horas)
    
    
    // **Autenticar Usuário e Obter server_token**
    if (performLogin()) {
        Serial.println("Autenticação completa e server_token obtido.");
    } else {
        Serial.println("Falha na autenticação inicial.");
        handleError(-1, "Falha na autenticação");
    }
    Serial.println("Aproxime um cartão NFC para ler.");
}

// **Função Principal de Loop**
// **Função Principal de Loop**
void loop() {
    int buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == LOW && lastButtonState == HIGH) {
        signMode = (signMode % 4) + 1;

        lcd.setCursor(0, 3); // Define o cursor para a linha 3
        lcd.print("                "); // Imprime espaços para limpar a linha

        // Define o cursor de volta para a linha 3 e imprime o novo modo
        lcd.setCursor(0, 3);
        lcd.print(signModeNames[signMode]); // Ajuste o índice conforme o array

        lastButtonPressTime = millis(); 
    }
    lastButtonState = buttonState;

    // Atualizar o cliente NTP e imprimir o horário no LCD
    printLocalTime();

    // Exibir nome da empresa no LCD
    lcd.setCursor(3, 1);
    lcd.print("EliteSports17  "); // Espaços para limpar possíveis resíduos

    // Verificar se é hora de renovar o server_token
    timeClient.update(); // Atualiza a hora do cliente NTP
    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();
    int currentDay = timeClient.getDay(); // Captura o dia atual

    // Verifica se estamos dentro do intervalo permitido (9h às 18h)
    /*if (currentHour >= 9 && currentHour <= 18) {
        // Verifica se já se passaram 15 minutos desde a última execução
        if (millis() - lastMelodyTime >= melodyInterval) {
            Serial.println("Tocando música de Natal...");
            playNextSong(); // Toca a próxima música na playlist
            lastMelodyTime = millis(); // Atualiza o tempo da última execução
        }
    }
*/
    // Verifica se é 6h da manhã e se o token não foi renovado hoje
    if (currentHour == 6 && !tokenRenewedToday) {
        Serial.println("Renovando server_token às 6h da manhã...");
        if (performLogin()) { // Realiza todo o processo de login novamente
            Serial.println("server_token renovado com sucesso.");
            tokenRenewedToday = true; // Marcar como renovado
        } else {
            Serial.println("Falha ao renovar server_token.");
            handleError(-1, "Falha na autenticação");
        }
    }

    // Verifica se já passou para um novo dia
    if (timeClient.getDay() != currentDay) {
        tokenRenewedToday = false; // Reseta a flag para permitir renovação no próximo dia
    }

    // Detecção e leitura do cartão NFC
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        Serial.println("Cartão NFC detectado!");
        String cardUID = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            cardUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "") + String(mfrc522.uid.uidByte[i], HEX);
            if (i < mfrc522.uid.size - 1) cardUID += ":";
        }
        Serial.println("UID do Cartão: " + cardUID);
        
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Aguardando respuesta");

        if (fetch_data(cardUID, signMode)) {
            Serial.println("Cartão autorizado, comunicação iniciada.");
        } else {
            Serial.println("Erro na leitura do cartão.");
        }

        mfrc522.PICC_HaltA(); // Parar a leitura do cartão
    }
}

