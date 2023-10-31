 // FileName:        HVAC_IO.c
 // Dependencies:    HVAC.h
 // Processor:       MSP432
 // Board:           MSP432P401R
 // Program version: CCS V8.3 TI
 // Company:         Texas Instruments
 // Description:     Funciones de control de HW a través de estados.
 // Authors:         José Luis Chacón M. y Jesús Alejandro Navarro Acosta.
 // Updated:         11/2018

#include "HVAC.h"

/* Variables sobre las cuales se maneja el sistema. */
float TemperaturaActual;       // Temperatura.
float SetPoint = 25.0;         // Valor deseado.

char state[MAX_MSG_SIZE];      // Cadena a imprimir.

bool toggle = 0;               // Toggle para el heartbeat.
uint32_t delay;                // Delay aplicado al heartbeat.
bool event = FALSE;            // Evento I/O que fuerza impresión inmediata.

bool FAN_LED_State = 0;                                     // Estado led_FAN.
const char* SysSTR[] = {"Cool","Off","Heat","Only Fan"};    // Control de los estados.

/* **** SE DECLARARON LAS VARIABLES Y FUNCIONES PARA REALIZAR EL DALAY CON EL TIMER ******** */
extern void Timer32_INT1 (void); // Función de interrupción.
extern void Delay_ms (uint32_t time); // Función de delay.
uint32_t tiempo = 2000;

/*FUNCTION******************************************************************************
*
* Function Name    : System_InicialiceTIMER
* Returned Value   : None.
* Comments         :
*    Controla los preparativos para poder usar TIMER32
*
*END***********************************************************************************/
void System_InicialiceTIMER (void)
{
    T32_Init1();
    Int_registerInterrupt(INT_T32_INT1, Timer32_INT1);
    Int_enableInterrupt(INT_T32_INT1);

}



/*******************************************************************************************/

/**********************************************************************************
 * Function: INT_SWI
 * Preconditions: Interrupción habilitada, registrada e inicialización de módulos.
 * Overview: Función que es llamada cuando se genera
 *           la interrupción del botón SW1 o SW2.
 * Input: None.
 * Output: None.
 **********************************************************************************/
void INT_SWI(void)
{
    GPIO_clear_interrupt_flag(P1,B1); // Limpia la bandera de la interrupción.
    GPIO_clear_interrupt_flag(P1,B4); // Limpia la bandera de la interrupción.

    if(!GPIO_getInputPinValue(SETPOINT_PORT,BIT(SP_UP)))        // Si se trata del botón para aumentar setpoint (SW1).
        HVAC_SetPointUp();
    else if(!GPIO_getInputPinValue(SETPOINT_PORT,BIT(SP_DOWN))) // Si se trata del botón para disminuir setpoint (SW2).
        HVAC_SetPointDown();

    /*UART_putsf(MAIN_UART,"Presionaste boton espera 2 segundos\n\r");
    Delay_ms(tiempo);*/
    return;
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_InicialiceIO
* Returned Value   : None.
* Comments         :
*    Controla los preparativos para poder usar las entradas y salidas GPIO.
*
*END***********************************************************************************/
void HVAC_InicialiceIO(void)
{
    // Para entradas y salidas ya definidas en la tarjeta.
    GPIO_init_board();

    // Modo de interrupción de los botones principales.
    GPIO_interruptEdgeSelect(SETPOINT_PORT,BIT(SP_UP),   GPIO_HIGH_TO_LOW_TRANSITION);
    GPIO_interruptEdgeSelect(SETPOINT_PORT,BIT(SP_DOWN), GPIO_HIGH_TO_LOW_TRANSITION);

    // Preparativos de interrupción.
    GPIO_clear_interrupt_flag(P1,B1);
    GPIO_clear_interrupt_flag(P1,B4);
    GPIO_enable_bit_interrupt(P1,B1);
    GPIO_enable_bit_interrupt(P1,B4);

    // Se necesitan más entradas, se usarán las siguientes:
    GPIO_setBitIO(FAN_PORT, FAN_ON, ENTRADA);
    GPIO_setBitIO(FAN_PORT, FAN_AUTO, ENTRADA);
    GPIO_setBitIO(SYSTEM_PORT, SYSTEM_COOL, ENTRADA);
    GPIO_setBitIO(SYSTEM_PORT, SYSTEM_OFF, ENTRADA);
    GPIO_setBitIO(SYSTEM_PORT, SYSTEM_HEAT, ENTRADA);
    GPIO_setBitIO(SETPOINT_PORT, SP_UP, ENTRADA);
    GPIO_setBitIO(SETPOINT_PORT, SP_DOWN, ENTRADA);

    /* Uso del módulo Interrupt para generar la interrupción general y registro de esta en una función
    *  que se llame cuando la interrupción se active.                                                   */
    Int_registerInterrupt(INT_PORT1, INT_SWI);
    Int_enableInterrupt(INT_PORT1);
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_InicialiceADC
* Returned Value   : None.
* Comments         :
*    Inicializa las configuraciones deseadas para
*    el módulo general ADC y dos de sus canales; uno para la temperatura, otro para
*    el heartbeat.
*
*END***********************************************************************************/
void HVAC_InicialiceADC(void)
{
    // Iniciando ADC y canales.
    ADC_Initialize(ADC_14bitResolution, ADC_CLKDiv8);
    ADC_SetConvertionMode(ADC_SequenceOfChannels);
    ADC_EnableTemperatureSensor(TEMP_CH);                          // Se configura el sensor en el canal 0.
    ADC_ConfigurePinChannel(HEARTBEAT_CH, POT_PIN, ADC_VCC_VSS);   // Pin AN1 para potenciómetro.
    ADC_SetEndOfSequenceChannel(HEARTBEAT_CH);                     // Termina en el AN1, canal último.
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_InicialiceUART
* Returned Value   : None.
* Comments         :
*    Inicializa las configuraciones deseadas para
*    configurar el modulo UART (comunicación asíncrona).
*
*END***********************************************************************************/
void HVAC_InicialiceUART (void)
{
    UART_init();
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_ActualizarEntradas
* Returned Value   : None.
* Comments         :
*    Actualiza los variables indicadores de las entradas sobre las cuales surgirán
*    las salidas.
*
*END***********************************************************************************/
void HVAC_ActualizarEntradas(void)
{
    static bool ultimos_estados[] = {FALSE, FALSE, FALSE, FALSE, FALSE};

    TemperaturaActual = ADC_GetTemperature(TEMP_CH);                                // Averigua temperatura actual.

    if(GPIO_getInputPinValue(FAN_PORT,BIT(FAN_ON)) != NORMAL_STATE_EXTRA_BUTTONS)   // Observa entradas.
    {
        EstadoEntradas.FanState = On;
        EstadoEntradas.SystemState = FanOnly;

        if(ultimos_estados[0] == FALSE)
            event = TRUE;

        ultimos_estados[0] = TRUE;
        ultimos_estados[1] = FALSE;
    }

    else if(GPIO_getInputPinValue(FAN_PORT,BIT(FAN_AUTO)) != NORMAL_STATE_EXTRA_BUTTONS)    // No hay default para
    {                                                                                       // cuando no se detecta
        EstadoEntradas.FanState = Auto;                                                     // ninguna entrada activa.
        if(ultimos_estados[1] == FALSE)
            event = TRUE;

        ultimos_estados[1] = TRUE;
        ultimos_estados[0] = FALSE;

        if(GPIO_getInputPinValue(SYSTEM_PORT,BIT(SYSTEM_COOL)))                             // Sistema en COOL.
        {
            EstadoEntradas.SystemState = Cool;
            if(ultimos_estados[2] == FALSE)
                event = TRUE;
            ultimos_estados[2] = TRUE;
            ultimos_estados[3] = FALSE;
            ultimos_estados[4] = FALSE;
        }
        else if(GPIO_getInputPinValue(SYSTEM_PORT,BIT(SYSTEM_OFF)))                         // Sistema apagado.
        {
            EstadoEntradas.SystemState = Off;
            if(ultimos_estados[3] == FALSE)
                event = TRUE;
            ultimos_estados[2] = FALSE;
            ultimos_estados[3] = TRUE;
            ultimos_estados[4] = FALSE;
        }
        else if(GPIO_getInputPinValue(SYSTEM_PORT,BIT(SYSTEM_HEAT)))                        // Sistema en HEAT.
        {
            EstadoEntradas.SystemState = Heat;
            if(ultimos_estados[4] == FALSE)
                event = TRUE;
            ultimos_estados[2] = FALSE;
            ultimos_estados[3] = FALSE;
            ultimos_estados[4] = TRUE;
        }
        else
        {
            EstadoEntradas.SystemState = Off;
            ultimos_estados[2] = FALSE;
            ultimos_estados[3] = FALSE;
            ultimos_estados[4] = FALSE;
        }                                                           // Este es solo un default en el caso de que
    }                                                               // el sistema no encuentre ningun estado de los 3
}                                                                   // activo (deberia ser un error debido a que debe
                                                                    // haber solo un botón activado siempre).

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_ActualizarSalidas
* Returned Value   : None.
* Comments         :
*    Decide a partir de las entradas actualizadas las salidas principales,
*    y en ciertos casos, en base a una cuestión de temperatura, la salida del 'fan'.
*
*END***********************************************************************************/
void HVAC_ActualizarSalidas(void)
{
    // Cambia el valor de las salidas de acuerdo a entradas.

    if(EstadoEntradas.FanState == On)
    {
        FAN_LED_State = 1;
        GPIO_setOutput(FAN_LED_PORT,  FAN_LED,  1);
        GPIO_setOutput(HEAT_LED_PORT, HEAT_LED, 0);
        GPIO_setOutput(COOL_LED_PORT, COOL_LED, 0);
    }

    else if(EstadoEntradas.FanState == Auto)
    {
        switch(EstadoEntradas.SystemState)
        {
        case Off:   GPIO_setOutput(FAN_LED_PORT,  FAN_LED,  0);
                    GPIO_setOutput(HEAT_LED_PORT, HEAT_LED, 0);
                    GPIO_setOutput(COOL_LED_PORT, COOL_LED, 0);
                    FAN_LED_State = 0;
                    break;
        case Heat:  HVAC_Heat(); break;
        case Cool:  HVAC_Cool(); break;
        }
    }
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_Heat
* Returned Value   : None.
* Comments         :
*    Decide a partir de la temperatura actual y la deseada, si se debe activar el fan.
*    (La temperatura deseada debe ser mayor a la actual). El estado del fan debe estar
*    en 'auto' y este modo debe estar activado para entrar a la función.
*
*END***********************************************************************************/
void HVAC_Heat(void)
{
    GPIO_setOutput(HEAT_LED_PORT, HEAT_LED, 1);
    GPIO_setOutput(COOL_LED_PORT, COOL_LED, 0);

    if(TemperaturaActual < SetPoint)                    // El fan se debe encender si se quiere una temp. más alta.
    {
        GPIO_setOutput(FAN_LED_PORT,  FAN_LED,  1);
        FAN_LED_State = 1;
    }
    else
    {
        GPIO_setOutput(FAN_LED_PORT,  FAN_LED,  0);
        FAN_LED_State = 0;
    }
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_Cool
* Returned Value   : None.
* Comments         :
*    Decide a partir de la temperatura actual y la deseada, si se debe activar el fan.
*    (La temperatura deseada debe ser menor a la actual). El estado del fan debe estar
*    en 'auto' y este modo debe estar activado para entrar a la función.
*
*END***********************************************************************************/
void HVAC_Cool(void)
{
    TemperaturaActual = ADC_GetTemperature(TEMP_CH);
    GPIO_setOutput(HEAT_LED_PORT, HEAT_LED, 0);
    GPIO_setOutput(COOL_LED_PORT, COOL_LED, 1);

    if(TemperaturaActual > SetPoint)                    // El fan se debe encender si se quiere una temp. más baja.
    {
        GPIO_setOutput(FAN_LED_PORT,  FAN_LED,  1);
        FAN_LED_State = 1;
    }
    else
    {
        GPIO_setOutput(FAN_LED_PORT,  FAN_LED,  0);
        FAN_LED_State = 0;
    }

}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_Heartbeat
* Returned Value   : None.
* Comments         :
*    Función que prende y apaga una salida para notificar que el sistema está activo.
*    El periodo en que se hace esto depende de una entrada del ADC en esta función.
*
*END***********************************************************************************/
void HVAC_Heartbeat(void)
{
    static int delay_en_curso = 0;

    ADC_trigger();
    while(ADC_is_busy());

    delay = 15000 + (100 * ADC_result(HEARTBEAT_CH) / 4);                // Lectura del ADC por medio de la función.

    delay_en_curso -= DELAY;
   if(delay_en_curso <= 0)
   {
       delay_en_curso = delay;
       toggle ^= 1;
   }

   GPIO_setOutput(HB_LED_PORT, HBeatLED, toggle);

   if(delay_en_curso < DELAY)
       usleep(delay_en_curso);
   else
       usleep(DELAY);
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_PrintState
* Returned Value   : None.
* Comments         :
*    Imprime via UART la situación actual del sistema en términos de temperaturas
*    actual y deseada, estado del abanico, del sistema y estado de las entradas.
*    Imprime cada cierto número de iteraciones y justo despues de recibir un cambio
*    en las entradas, produciéndose un inicio en las iteraciones.
*END***********************************************************************************/
void HVAC_PrintState(void)
{
    static char iterations = 0;

    iterations++;
    if(iterations >= ITERATIONS_TO_PRINT || event == TRUE)
    {
       if (event == TRUE)
       {
           UART_putsf(MAIN_UART,"Presionaste el boton espera 2 segundos\n\r");  //     <----   PAUSA CON EL TIMER32
           Delay_ms(tiempo);
       }
        iterations = 0;
        event = FALSE;

        sprintf(state,"Fan: %s, System: %s, SetPoint: %0.2f\n\r",
                    EstadoEntradas.FanState == On? "On":"Auto",
                    SysSTR[EstadoEntradas.SystemState],
                    SetPoint);
        UART_putsf(MAIN_UART,state);


        sprintf(state,"Temperatura Actual: %0.2f°C %0.2f°F  Fan: %s\n\r\n\r",
                    TemperaturaActual,
                    ((TemperaturaActual*9.0/5.0) + 32),
                    FAN_LED_State?"On":"Off");
        UART_putsf(MAIN_UART,state);


    }
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_SetPointUp
* Returned Value   : None.
* Comments         :
*    Sube el valor deseado (set point). Llamado por interrupción a causa del SW1.
*
*END***********************************************************************************/
void HVAC_SetPointUp(void)
{
    SetPoint += 0.5;
    event = TRUE;
}

/*FUNCTION******************************************************************************
*
* Function Name    : HVAC_SetPointDown
* Returned Value   : None.
* Comments         :
*    Baja el valor deseado (set point). Llamado por interrupción a causa del SW2.
*
*END***********************************************************************************/
void HVAC_SetPointDown(void)
{
    SetPoint -= 0.5;
    event = TRUE;
}
