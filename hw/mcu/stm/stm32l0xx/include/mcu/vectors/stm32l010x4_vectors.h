/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

INT_VECTOR_STACK_TOP(__StackTop)
INT_VECTOR_RESET_HANDLER(Reset_Handler)
INT_VECTOR_NMI_HANDLER(NMI_Handler)
INT_VECTOR_HARDFAULT_HANDLER(HardFault_Handler)
INT_VECTOR_MEMMANAGE_HANDLER(MemManage_Handler)
INT_VECTOR_BUSFAULT_HANDLER(BusFault_Handler)
INT_VECTOR_USAGEFAULT_HANDLER(UsageFault_Handler)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR_SVC_HANDLER(SVC_Handler)
INT_VECTOR_DEBUGMON_HANDLER(DebugMon_Handler)
INT_VECTOR_UNUSED(0)
INT_VECTOR_PENDSV_HANDLER(PendSV_Handler)
INT_VECTOR_SYSTICK_HANDLER(SysTick_Handler)
INT_VECTOR(WWDG_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR(RTC_IRQHandler)
INT_VECTOR(FLASH_IRQHandler)
INT_VECTOR(RCC_IRQHandler)
INT_VECTOR(EXTI0_1_IRQHandler)
INT_VECTOR(EXTI2_3_IRQHandler)
INT_VECTOR(EXTI4_15_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR(DMA1_Channel1_IRQHandler)
INT_VECTOR(DMA1_Channel2_3_IRQHandler)
INT_VECTOR(DMA1_Channel4_5_IRQHandler)
INT_VECTOR(ADC1_IRQHandler)
INT_VECTOR(LPTIM1_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR(TIM2_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR(TIM21_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR(I2C1_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR(SPI1_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
INT_VECTOR(USART2_IRQHandler)
INT_VECTOR(LPUART1_IRQHandler)
INT_VECTOR_UNUSED(0)
INT_VECTOR_UNUSED(0)
