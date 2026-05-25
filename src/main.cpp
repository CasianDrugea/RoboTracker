#include <Arduino.h>

// --- CONFIGURARE PINI ---
const int right_fr = 9; 
const int left_fr  = 3; 
const int right_bk = 2; 
const int left_bk  = 5; 

#define TRIG_PIN 8
#define ECHO_PIN 6         
#define CONST_DISTANCE 15 
#define LED 13

volatile unsigned long pulse_start = 0;
volatile int distanta_reala = 100;
volatile bool stop_de_urgenta = false; 

enum StareRobot { STAI, INAINTE, INAPOI, TRACKING, TURN_L, TURN_R };
volatile StareRobot regimCurent = STAI; 

int vitezaGlobala = 230; 
unsigned long timerSpecial = 0;

// --- CALIBRARE CURBĂ 90 GRADE ---
const unsigned long DURATA_90_CURBA = 2800; 
const float COEFICIENT_CURBA = 0.15; 

void stopMotoare();
void executaMiscare(int vr_f, int vr_b, int vl_f, int vl_b);
void proceseazaTasta(char tasta);

// --- ISR: FRÂNĂ HARDWARE ASINCRONĂ ---
ISR(PCINT2_vect) {
    if (PIND & _BV(PIND6)) { 
        pulse_start = micros();
    } else {
        unsigned long durata = micros() - pulse_start;
        distanta_reala = durata / 58;
        
        // Protecția acționează la mersul înainte sau în viraje
        if (regimCurent == INAINTE || regimCurent == TURN_L || regimCurent == TURN_R) {
            if (distanta_reala > 0 && distanta_reala < CONST_DISTANCE) {
                stop_de_urgenta = true; 
            }
        }
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(right_fr, OUTPUT); 
    pinMode(right_bk, OUTPUT);
    pinMode(left_fr, OUTPUT);  
    pinMode(left_bk, OUTPUT);
    pinMode(TRIG_PIN, OUTPUT); 
    pinMode(ECHO_PIN, INPUT_PULLUP);
    pinMode(LED, OUTPUT);

    // Activare PCINT22 pe Pinul 6
    cli(); 
    PCICR  |= (1 << PCIE2); 
    PCMSK2 |= (1 << PCINT22); 
    sei();

    Serial.println("RoboTracker Gata. Comenzi: W, A, S, D, T, X");
}

void loop() {
    // 1. Refresh Senzor Ultrasonic
    digitalWrite(TRIG_PIN, LOW); 
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); 
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // 2. Gestionare Siguranță
    if (stop_de_urgenta && (regimCurent != STAI && regimCurent != INAPOI)) {
        regimCurent = STAI;
        digitalWrite(LED, HIGH);
        Serial.println("!!! BLOCAJ: Obstacol detectat !!!");
    }

    // 3. Citire Comenzi UART
    if (Serial.available() > 0) {
        char c = Serial.read();
        if (c != '\n' && c != '\r') proceseazaTasta(c);
    }

    // 4. Mașina de Stări (FSM)
    switch (regimCurent) {
        case INAINTE:
            executaMiscare(vitezaGlobala, 0, vitezaGlobala, 0); 
            break;

        case INAPOI:
            executaMiscare(0, vitezaGlobala, 0, vitezaGlobala);
            break;

        case TURN_L: 
            // Curbă 90 grade stânga (Roata Dreapta 100%, Roata Stânga 15%)
            executaMiscare(vitezaGlobala, 0, (int)(vitezaGlobala * COEFICIENT_CURBA), 0);
            if (millis() - timerSpecial > DURATA_90_CURBA) regimCurent = STAI;
            break;

        case TURN_R: 
            // Curbă 90 grade dreapta (Roata Stânga 100%, Roata Dreapta 15%)
            executaMiscare((int)(vitezaGlobala * COEFICIENT_CURBA), 0, vitezaGlobala, 0);
            if (millis() - timerSpecial > DURATA_90_CURBA) regimCurent = STAI;
            break;

        case TRACKING:
            if (distanta_reala > 0 && distanta_reala < 8) {
                executaMiscare(0, 200, 0, 200); // Prea aproape -> Retragere
            } 
            else if (distanta_reala >= 8 && distanta_reala <= 16) {
                stopMotoare(); // Zonă OK -> Stop
            } 
            else if (distanta_reala > 16 && distanta_reala <= 50) {
                executaMiscare(200, 0, 200, 0); // Prea departe -> Urmărire
            } 
            else {
                stopMotoare(); // Obiect pierdut
            }
            break;

        case STAI:
            stopMotoare();
            break;
    }
}

void proceseazaTasta(char tasta) {
    stop_de_urgenta = false;
    digitalWrite(LED, LOW);

    // Pornim timer-ul doar pentru manevrele de viraj temporizate
    if (tasta == 'a' || tasta == 'A' || tasta == 'd' || tasta == 'D') {
        timerSpecial = millis();
    }

    switch (tasta) {
        case 'w': case 'W': 
            regimCurent = (distanta_reala > CONST_DISTANCE) ? INAINTE : STAI; 
            break;
        case 's': case 'S': 
            regimCurent = INAPOI; 
            break;
        case 'a': case 'A': 
            regimCurent = TURN_L; 
            break;
        case 'd': case 'D': 
            regimCurent = TURN_R; 
            break;
        case 't': case 'T': 
            regimCurent = TRACKING; 
            Serial.println("Mod: Tracking ACTIV");
            break;
        case 'x': case 'X': 
            regimCurent = STAI; 
            break;
    }
}

void executaMiscare(int vr_f, int vr_b, int vl_f, int vl_b) {
    // Excepție: Tracking-ul ignoră stop_de_urgenta hardware deoarece își gestionează singur distanța
    if (regimCurent != TRACKING) {
        if (stop_de_urgenta && (vr_f > 0 || vl_f > 0)) {
            stopMotoare();
            return;
        }
    }

    analogWrite(right_fr, vr_f);
    digitalWrite(right_bk, vr_b > 0 ? HIGH : LOW);
    analogWrite(left_fr, vl_f);
    analogWrite(left_bk, vl_b);
}

void stopMotoare() {
    analogWrite(right_fr, 0);
    digitalWrite(right_bk, LOW);
    analogWrite(left_fr, 0);
    analogWrite(left_bk, 0);
}