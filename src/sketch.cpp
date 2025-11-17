// Bibliotecas
#include <WiFi.h>
#include <PubSubClient.h>

// 1. CREDENCIAIS
const char *WIFI_SSID = "Inteli.Iot";
const char *WIFI_PASS = "%(Yk(sxGMtvFEs.3";
const char *UBIDOTS_TOKEN = "BBUS-gr0GRvMirUFz00KQ8dT2kTXCUMdBZh";
const char *DEVICE_LABEL = "cruzamento";

// Pinos Semáforos
#define S1_VERMELHO 15
#define S1_AMARELO 2
#define S1_VERDE 0
#define S2_VERMELHO 26
#define S2_AMARELO 25
#define S2_VERDE 33
#define S3_VERMELHO 16
#define S3_AMARELO 17
#define S3_VERDE 5
#define S4_VERMELHO 12
#define S4_AMARELO 14
#define S4_VERDE 27

// Pinos Sensor HC-SR04
#define TRIG_PIN 23
#define ECHO_PIN 21

// Configurações MQTT
const char *MQTT_BROKER = "industrial.api.ubidots.com";
const int MQTT_PORT = 1883;
const char *VAR_DISTANCIA = "distancia";
const char *VAR_FORCAR_NOITE = "forcar-noite"; // Tópico de controle

// Constantes de Tempo (em milissegundos)
#define TEMPO_VERDE 5000
#define TEMPO_AMARELO 1500
#define TEMPO_PISCA 500

// Limiar do Sensor
#define DISTANCIA_NOTURNA 6.0

// Objetos de Rede
WiFiClient espClient;
PubSubClient client(espClient);

// Variáveis Globais de Controle
int estadoSemaforo = 0;
unsigned long tempoAnterior = 0;
unsigned long tempoPisca = 0;
bool amareloLigado = false;
bool estavaModoNoturno = false;
bool forceNightMode = false; // <-- NOVO: Flag para forçar modo noturno

// Timers Não-Bloqueantes
unsigned long lastPublish = 0;
unsigned long lastSerialPrint = 0;
long PUBLISH_INTERVAL = 3000;
long SERIAL_PRINT_INTERVAL = 500;

// DECLARAÇÃO DE FUNÇÕES (para o callback)
void callback(char *topic, byte *payload, unsigned int length); // <-- NOVO

// SETUP: Executa uma vez
void setup()
{
    Serial.begin(115200);

    // ... (Configura todos os pinos de LED como OUTPUT) ...
    pinMode(S1_VERMELHO, OUTPUT);
    pinMode(S1_AMARELO, OUTPUT);
    pinMode(S1_VERDE, OUTPUT);
    pinMode(S2_VERMELHO, OUTPUT);
    pinMode(S2_AMARELO, OUTPUT);
    pinMode(S2_VERDE, OUTPUT);
    pinMode(S3_VERMELHO, OUTPUT);
    pinMode(S3_AMARELO, OUTPUT);
    pinMode(S3_VERDE, OUTPUT);
    pinMode(S4_VERMELHO, OUTPUT);
    pinMode(S4_AMARELO, OUTPUT);
    pinMode(S4_VERDE, OUTPUT);

    // Configura Sensor
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    // Inicia Wi-Fi
    setup_wifi();

    // Configura MQTT
    client.setServer(MQTT_BROKER, MQTT_PORT);
    client.setCallback(callback); // <-- NOVO: Define a função que ouve mensagens

    // Inicia o sistema
    setEstadoSemaforos(0);
    tempoAnterior = millis();
}

// LOOP: Executa continuamente
void loop()
{
    if (!client.connected())
    {
        reconnect();
    }
    client.loop(); // Essencial para o MQTT (ouvir e manter vivo)

    // 2. Lógica Principal
    float distancia = lerDistancia();
    bool sensorDetectou = (distancia < DISTANCIA_NOTURNA && distancia > 0);

    // Imprime no Serial (controlado por timer)
    if (millis() - lastSerialPrint > SERIAL_PRINT_INTERVAL)
    {
        Serial.print("Distância: ");
        Serial.print(distancia);
        Serial.println(" cm");
        Serial.print("Modo Forçado: ");
        Serial.println(forceNightMode ? "Sim" : "Nao");
        lastSerialPrint = millis();
    }

    // Ativa o modo noturno SE o sensor detectar OU se o botão da dashboard estiver ligado
    bool modoNoturnoAtual = (sensorDetectou || forceNightMode);

    if (modoNoturnoAtual)
    {
        executarModoNoturno();
    }
    else
    {
        executarModoDia();
    }

    // 3. Publicação de Dados (controlado por timer)
    if (millis() - lastPublish > PUBLISH_INTERVAL)
    {
        publishData(distancia);
        lastPublish = millis();
    }
}

// Funções Principais de Lógica (executarModoDia, executarModoNoturno)
void executarModoDia()
{
    if (estavaModoNoturno)
    {
        Serial.println("Retornando ao Modo Dia.");
        setEstadoSemaforos(0);
        estadoSemaforo = 0;
        tempoAnterior = millis();
        estavaModoNoturno = false;
    }

    // Máquina de estados
    unsigned long tempoAtual = millis();

    if (estadoSemaforo == 0)
    { // A Verde
        if (tempoAtual - tempoAnterior >= TEMPO_VERDE)
        {
            setEstadoSemaforos(1);
            estadoSemaforo = 1;
            tempoAnterior = tempoAtual;
        }
    }
    else if (estadoSemaforo == 1)
    { // A Amarelo
        if (tempoAtual - tempoAnterior >= TEMPO_AMARELO)
        {
            setEstadoSemaforos(2);
            estadoSemaforo = 2;
            tempoAnterior = tempoAtual;
        }
    }
    else if (estadoSemaforo == 2)
    { // B Verde
        if (tempoAtual - tempoAnterior >= TEMPO_VERDE)
        {
            setEstadoSemaforos(3);
            estadoSemaforo = 3;
            tempoAnterior = tempoAtual;
        }
    }
    else if (estadoSemaforo == 3)
    { // B Amarelo
        if (tempoAtual - tempoAnterior >= TEMPO_AMARELO)
        {
            setEstadoSemaforos(0);
            estadoSemaforo = 0;
            tempoAnterior = tempoAtual;
        }
    }
}

void executarModoNoturno()
{
    if (!estavaModoNoturno)
    {
        Serial.println("Entrando no Modo Noturno!");
    }
    estavaModoNoturno = true;

    // Lógica do pisca-pisca
    if (millis() - tempoPisca >= TEMPO_PISCA)
    {
        amareloLigado = !amareloLigado;

        // Apaga todas as luzes verdes e vermelhas
        digitalWrite(S1_VERDE, LOW);
        digitalWrite(S1_VERMELHO, LOW);
        digitalWrite(S2_VERDE, LOW);
        digitalWrite(S2_VERMELHO, LOW);
        digitalWrite(S3_VERDE, LOW);
        digitalWrite(S3_VERMELHO, LOW);
        digitalWrite(S4_VERDE, LOW);
        digitalWrite(S4_VERMELHO, LOW);

        // Liga/Desliga todos os amarelos juntos
        digitalWrite(S1_AMARELO, amareloLigado);
        digitalWrite(S2_AMARELO, amareloLigado);
        digitalWrite(S3_AMARELO, amareloLigado);
        digitalWrite(S4_AMARELO, amareloLigado);

        tempoPisca = millis();
    }
}

// Funções Auxiliares (LEDs, Sensor, WiFi, MQTT)
void setEstadoSemaforos(int estado)
{
    // 0=A Verde, B Vermelho
    if (estado == 0)
    {
        digitalWrite(S1_VERDE, HIGH);
        digitalWrite(S1_AMARELO, LOW);
        digitalWrite(S1_VERMELHO, LOW);
        digitalWrite(S3_VERDE, HIGH);
        digitalWrite(S3_AMARELO, LOW);
        digitalWrite(S3_VERMELHO, LOW);
        digitalWrite(S2_VERDE, LOW);
        digitalWrite(S2_AMARELO, LOW);
        digitalWrite(S2_VERMELHO, HIGH);
        digitalWrite(S4_VERDE, LOW);
        digitalWrite(S4_AMARELO, LOW);
        digitalWrite(S4_VERMELHO, HIGH);
    }
    // 1=A Amarelo, B Vermelho
    else if (estado == 1)
    {
        digitalWrite(S1_VERDE, LOW);
        digitalWrite(S1_AMARELO, HIGH);
        digitalWrite(S1_VERMELHO, LOW);
        digitalWrite(S3_VERDE, LOW);
        digitalWrite(S3_AMARELO, HIGH);
        digitalWrite(S3_VERMELHO, LOW);
        digitalWrite(S2_VERDE, LOW);
        digitalWrite(S2_AMARELO, LOW);
        digitalWrite(S2_VERMELHO, HIGH);
        digitalWrite(S4_VERDE, LOW);
        digitalWrite(S4_AMARELO, LOW);
        digitalWrite(S4_VERMELHO, HIGH);
    }
    // 2=B Verde, A Vermelho
    else if (estado == 2)
    {
        digitalWrite(S1_VERDE, LOW);
        digitalWrite(S1_AMARELO, LOW);
        digitalWrite(S1_VERMELHO, HIGH);
        digitalWrite(S3_VERDE, LOW);
        digitalWrite(S3_AMARELO, LOW);
        digitalWrite(S3_VERMELHO, HIGH);
        digitalWrite(S2_VERDE, HIGH);
        digitalWrite(S2_AMARELO, LOW);
        digitalWrite(S2_VERMELHO, LOW);
        digitalWrite(S4_VERDE, HIGH);
        digitalWrite(S4_AMARELO, LOW);
        digitalWrite(S4_VERMELHO, LOW);
    }
    // 3=B Amarelo, A Vermelho
    else if (estado == 3)
    {
        digitalWrite(S1_VERDE, LOW);
        digitalWrite(S1_AMARELO, LOW);
        digitalWrite(S1_VERMELHO, HIGH);
        digitalWrite(S3_VERDE, LOW);
        digitalWrite(S3_AMARELO, LOW);
        digitalWrite(S3_VERMELHO, HIGH);
        digitalWrite(S2_VERDE, LOW);
        digitalWrite(S2_AMARELO, HIGH);
        digitalWrite(S2_VERMELHO, LOW);
        digitalWrite(S4_VERDE, LOW);
        digitalWrite(S4_AMARELO, HIGH);
        digitalWrite(S4_VERMELHO, LOW);
    }
}

float lerDistancia()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duracao = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
    float distancia = (duracao * 0.01715);
    return distancia;
}

void setup_wifi()
{
    delay(10);
    Serial.println();
    Serial.print("Conectando ao Wi-Fi: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi conectado!");
}

// Funções de Callback e Subscrição
void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Mensagem recebida no tópico: ");
    Serial.println(topic);

    // Converte o payload (byte*) para uma string (char*)
    char message[length + 1];
    strncpy(message, (char *)payload, length);
    message[length] = '\0';
    Serial.print("Mensagem: ");
    Serial.println(message); // Veja o que o Ubidots está a enviar!

    // Em vez de comparar texto, convertemos a mensagem para um número
    float value = atof(message);

    if (value == 1.0)
    { // Comparamos o número
        forceNightMode = true;
        Serial.println("Modo Noturno FORÇADO via Dashboard");
    }
    else
    {
        forceNightMode = false;
        Serial.println("Modo Noturno DESLIGADO via Dashboard");
    }
}

void reconnect()
{
    while (!client.connected())
    {
        Serial.print("Tentando conexão MQTT...");
        if (client.connect("esp32-semaforo-client", UBIDOTS_TOKEN, "1"))
        {
            Serial.println("Conectado!");

            // O Ubidots usa "/lv" (last value) no final do tópico para subscrever
            char topicToSubscribe[150];
            snprintf(topicToSubscribe, sizeof(topicToSubscribe), "/v1.6/devices/%s/%s/lv", DEVICE_LABEL, VAR_FORCAR_NOITE);

            client.subscribe(topicToSubscribe); // Se inscreve no tópico
            Serial.print("Inscrito no tópico: ");
            Serial.println(topicToSubscribe);
        }
        else
        {
            Serial.print("Falhou, rc=");
            Serial.print(client.state());
            Serial.println(" Tente novamente em 5 segundos");
            delay(5000);
        }
    }
}

void publishData(float distancia)
{
    char topic[150];
    char payload[100];

    // 1. Publica a Distância
    snprintf(topic, sizeof(topic), "/v1.6/devices/%s/%s", DEVICE_LABEL, VAR_DISTANCIA);
    snprintf(payload, sizeof(payload), "%.2f", distancia);
    client.publish(topic, payload);

    Serial.println("--> Dados publicados no Ubidots <--");
}