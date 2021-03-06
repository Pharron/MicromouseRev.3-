/*******************************************************************************
  * File Name          : main.cpp
  * Description        : Main program body
  *****************************************************************************/


/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <vector>
#include "stm32l4xx_hal.h"

/* Private variables ---------------------------------------------------------*/
#define MAP_SIZE 16
#define MOVE_STACK_SIZE 32
#define WALL_THRESHOLD_S 500
#define WALL_THRESHOLD_L 3000
#define ONE_SQUARE 100
#define TURN_INSIDE 10
#define TURN_OUTSIDE 200
#define TURN_AROUND 150
#define NORTH 0x0
#define EAST 0x1
#define SOUTH 0x2
#define WEST 0x3

#define BASE_SPEED 60

ADC_HandleTypeDef hadc1;
TIM_HandleTypeDef htim1;

static volatile uint32_t enCountRight = 0;
static volatile uint32_t enCountLeft = 0;
static bool rightMotorFinish = 0;
static bool leftMotorFinish = 0;

static uint8_t currentXpos;
static uint8_t currentYpos;

static uint8_t direction;
static uint8_t defaultDir;

enum Movement {noMove,forward,turnRight,turnLeft,turnAround};

struct movementVector {
  uint8_t pwmR1;
  uint8_t pwmR2;
  uint8_t pwmL1;
  uint8_t pwmL2;
  uint16_t rightMotorSteps;
  uint16_t leftMotorSteps;
  Movement moveType;
};

struct analogValues {
	uint16_t rightBackIRVal;
	uint16_t rightFrontIRVal;
	uint16_t middleIRVal;
	uint16_t leftFrontIRVal;
	uint16_t leftBackIRVal;
};

struct map {
	uint8_t xPos;
	uint8_t yPos;
	uint16_t fillVal;
	uint8_t walls;        // bits   X,X,X,X,NORTH,EAST,SOUTH,WEST  wall=1
	bool scanned;
};

static movementVector forwardMove;
static movementVector turnRightMove;
static movementVector turnLeftMove;
static movementVector turnAroundMove;

analogValues analog1;

std::vector<movementVector> moveStack;
std::vector<map> floodStack;

map MAP [MAP_SIZE][MAP_SIZE] = {};    

/* Private function prototypes -----------------------------------------------*/
void TEST(void);

void SystemClock_Config(void);
static void GPIO_Init(void);
static void ADC1_Init(void);
static void TIM1_Init(void);
static void EXTI_Init(void);
static void Struct_Init(void);

                                    
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
                                
static void genMoveVector(void);
static void genStartVector(void);
static void genRunVector(void);
	
static void exeMoveVector(void);

static void waitForButton(void);
static void mapCell(void);
static int checkMapComplete(void);
static void analogRead(void);
static void resetEnCounts(void);
static void setMotorMove(movementVector);
static void setNewPos(Movement);

/***********************************************************************************
**                                   MAIN                                         **
***********************************************************************************/
int main(void)
{
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  GPIO_Init();
  ADC1_Init();
  TIM1_Init();
	EXTI_Init();
	
	forwardMove.pwmL1 = BASE_SPEED;
	forwardMove.pwmL2 = 0;
	forwardMove.pwmR1 = BASE_SPEED;
	forwardMove.pwmR2 = 0;
	forwardMove.leftMotorSteps = ONE_SQUARE;
	forwardMove.rightMotorSteps = ONE_SQUARE;
	forwardMove.moveType = forward;
	
	turnRightMove.pwmL1 = BASE_SPEED;
	turnRightMove.pwmL2 = 0;
	turnRightMove.pwmR1 = BASE_SPEED*(TURN_OUTSIDE/TURN_INSIDE);
	turnRightMove.pwmR2 = 0;
	turnRightMove.leftMotorSteps = TURN_OUTSIDE;
	turnRightMove.rightMotorSteps = TURN_INSIDE;
	turnRightMove.moveType = turnRight;
	
	turnLeftMove.pwmL1 = BASE_SPEED*(TURN_OUTSIDE/TURN_INSIDE);
	turnLeftMove.pwmL2 = 0;
	turnLeftMove.pwmR1 = BASE_SPEED;
	turnLeftMove.pwmR2 = 0;
	turnLeftMove.leftMotorSteps = TURN_INSIDE;
	turnLeftMove.rightMotorSteps = TURN_OUTSIDE;
	turnLeftMove.moveType = turnLeft;
	
	turnAroundMove.pwmL1 = BASE_SPEED;
	turnAroundMove.pwmL2 = 0;
	turnAroundMove.pwmR1 = 0;
	turnAroundMove.pwmR2 = BASE_SPEED;
	turnAroundMove.leftMotorSteps = TURN_AROUND;
	turnAroundMove.rightMotorSteps = TURN_AROUND;
	turnAroundMove.moveType = turnAround;
	
	
	//TEST();

	if((GPIOB->IDR&0x80) == 0x80)
	{
		defaultDir = WEST;
	}
	else
	{
		defaultDir = NORTH;
	}

	currentXpos = 0;
	currentYpos = 0;
	direction = defaultDir;
  
	waitForButton();
	while(1)
	{
		//MAPPING MODE 00
		while((GPIOB->IDR&0xC0) == 0x00) 
		{
			mapCell();
			genMoveVector();
			exeMoveVector();
			while((checkMapComplete()==1)&&((GPIOB->IDR&0xC0) == 0x00))
			{
				if((currentXpos != 0)&&(currentYpos != 0)&&(direction != defaultDir))

				{
					genStartVector();
					exeMoveVector();
				}
				HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_SET);
				HAL_Delay(500);
				HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_RESET);
				HAL_Delay(500);
			}
		}
		//SOLVE MODE 01
		while((GPIOB->IDR&0xC0) == 0x40) 
		{
			waitForButton();
			genRunVector();
			exeMoveVector();
		}
	}
}
/***********************************************************************************
**                               MAIN END                                         **
***********************************************************************************/

/***********************************************************************************
Function   :  TEST()
Description:  Does Test things
Inputs     :  None
Outputs    :  None
***********************************************************************************/
void TEST()
{
	while(1);
}

/***********************************************************************************
Function   :  mapCell()
Description:  reads the ADCs and populates the current cell with wall data
Inputs     :  None
Outputs    :  None

Status     :  Complete, works as intended for the current implementation
***********************************************************************************/
void mapCell(void)
{

	if(MAP[currentXpos][currentYpos].scanned == 1) //if current map position has not been mapped
	{ 
		MAP[currentXpos][currentYpos].scanned = 1;
		analogRead();
		switch(direction) 
		{
			case NORTH:
				if(analog1.middleIRVal<=WALL_THRESHOLD_S)
				{
					MAP[currentXpos][currentYpos].walls|=0x08;
				}
				if(analog1.leftFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x01;
				}
				if(analog1.rightFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x04;
				}
				break;
			case WEST:
				if(analog1.middleIRVal<=WALL_THRESHOLD_S) 
				{

					MAP[currentXpos][currentYpos].walls|=0x01;
				}
				if(analog1.leftFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x02;
				}
				if(analog1.rightFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x08;
				}
				break;
			case SOUTH:
				if(analog1.middleIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x02;
				}
				if(analog1.leftFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x04;
				}
				if(analog1.rightFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x01;
				}
				break;
			case EAST:
				if(analog1.middleIRVal<=WALL_THRESHOLD_S) 
				{

					MAP[currentXpos][currentYpos].walls|=0x04;
				}
				if(analog1.leftFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x08;
				}
				if(analog1.rightFrontIRVal<=WALL_THRESHOLD_S) 
				{
					MAP[currentXpos][currentYpos].walls|=0x02;
				}
				break;
		}
	}
}

/***********************************************************************************
Function   :  waitForButton()
Description:  runs loop waiting for the button to be pressed. After the
              button is pressed, it will delay for 3 seconds then return.
Inputs     :  None
Outputs    :  None

Status     :  Complete
***********************************************************************************/
void waitForButton(void)
{
	//loop while button is not pressed 
	while((GPIOA->IDR&0x1000)==0x0000)
	{
		//empty on purpose
	}
	//delay for final adjustments
	HAL_Delay(3000);
}

/***********************************************************************************
Function   :  exeMoveVector()
Description:  runs the motors to move to the next cell
Inputs     :  None
Outputs    :  None

Status     :  Mostly Complete, needs movement control system code
***********************************************************************************/
void exeMoveVector(void)
{
	movementVector currentMove;
	
	//Repeats while there is still movements on the stack to be executed
	while(moveStack.empty() == 0)
	{
		currentMove = moveStack.back();   //gets the next movement to execute
		moveStack.pop_back();             //deletes the movement we loaded off the stack
		rightMotorFinish = 0;             //clears movement complete flags
		leftMotorFinish = 0;
		resetEnCounts();                  //resets the encoder counters 
		setMotorMove(currentMove);        //sets the PWMs for the movement
		setNewPos(currentMove.moveType);  //sets the position of the uM to the destination
		
		//loops until the movement has completed
		while((rightMotorFinish == 0)&&(leftMotorFinish))
		{
			//movement control system goes here
			
			//checks to see if the movement is done
			if(currentMove.rightMotorSteps<=enCountRight)
			{
				rightMotorFinish = 1;
			}
			if(currentMove.leftMotorSteps<=enCountLeft)
			{
				leftMotorFinish = 1;
			}
		}
	}
}

/***********************************************************************************
Function   :  checkMapComplete()
Description:  Checks to see if the current floodfill solution to the maze has all 
              cells in the path mapped
Inputs     :  None
Outputs    :  returns a 1 or 0

Status     :  Not Started
***********************************************************************************/
int checkMapComplete(void)
{
	return 0;
}

/***********************************************************************************
Function   :  setMotorMove()
Description:  Sets the PWMs up for the next movement 
Inputs     :  move
Outputs    :  None

Status     :  Not Started
***********************************************************************************/
void setMotorMove(movementVector move)
{
	
}

/***********************************************************************************
Function   :  resetFillVals()
Description:  Resets the floodfill values to prevent old flood fill instances from 
              interfering with the current process
Inputs     :  None
Outputs    :  None

Status     :  Complete
***********************************************************************************/
void resetFillVals(void)
{
	for(int i = 0;i<MAP_SIZE;i++)
	{
		for(int j = 0;j<MAP_SIZE;j++)
		{
			MAP[i][j].fillVal = 0;
		}
	}
}

/***********************************************************************************
Function   :  genMoveVector()
Description:  generates the movement steps to get to the next unmapped cell of the maze
Inputs     :  None
Outputs    :  None


Status     :  mostly done, will have errors because it doesn't account for starting direction of uMouse
***********************************************************************************/
void genMoveVector(void)
{
	//reset the fillVals so that past iterations of flood fill dont interfere
	resetFillVals();
	map workingCell, targetCell, previousCell,nextCell;
	bool targetPosFound = 0;
	bool turned = 0;
	
	//load current position as the starting point and set fillVal
	MAP[currentXpos][currentYpos].fillVal = 0x8000;
	floodStack.insert(0,MAP[currentXpos][currentYpos]);
	
	//while the destination hasn't been found yet
	while(targetPosFound == 0)
	{
		//load a cell off the stack 
		workingCell = floodStack.back();
		floodStack.pop_back();
		
		//if the cell that was loaded has not been scanned, set as destination
		if(workingCell.scanned == 0)
		{
			targetCell = workingCell;
			targetPosFound = 1;
		}
		
		//if there is a path from the currently loaded cell and the fill value of that cell is 0, set fillVal and add to stack.
		for(int j = 0;j<4;j++)
		{
			// checks the walls in the order of WEST, SOUTH, EAST, NORTH
			if((workingCell.walls&(0x1<<j)) != (0x1<<j))
			{
				switch(j)
				{
					case(0):
						if(MAP[workingCell.xPos-1][workingCell.yPos].fillVal != 0)
						{
							MAP[workingCell.xPos-1][workingCell.yPos].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos-1][workingCell.yPos]);
						}
					break;
					case(1):
						if(MAP[workingCell.xPos][workingCell.yPos-1].fillVal != 0)
						{
							MAP[workingCell.xPos][workingCell.yPos-1].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos][workingCell.yPos-1]);
						}
					break;
					case(2):
						if(MAP[workingCell.xPos+1][workingCell.yPos].fillVal != 0)
						{
							MAP[workingCell.xPos+1][workingCell.yPos].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos+1][workingCell.yPos]);
						}
					break;
					case(3):
						if(MAP[workingCell.xPos][workingCell.yPos+1].fillVal != 0)
						{
							MAP[workingCell.xPos][workingCell.yPos+1].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos][workingCell.yPos+1]);
						}
					break;
				}
			}	
		}
	}
	
	//fills the moveStack with The steps needed to get to the target cell
	for(int i = 0;i<targetCell.fillVal;i++)
	{
		//next cell West
		if(MAP[workingCell.xPos-1][workingCell.yPos].fillVal == (workingCell.fillVal-1))
		{
			if(previousCell.fillVal != 0)
			{
				if(previousCell.xPos == workingCell.xPos)
				{
					if((previousCell.yPos>workingCell.yPos)&&(turned == 0))
					{
						moveStack.push_back(turnLeftMove);
						turned = 1;
					}
					if((previousCell.yPos<workingCell.yPos)&&(turned == 0))
					{
						moveStack.push_back(turnRightMove);
						turned = 1;
					}
				}
			}
			moveStack.push_back(forwardMove);
			previousCell = workingCell;
			workingCell = MAP[workingCell.xPos-1][workingCell.yPos];
		}
		//next cell East
		if(MAP[workingCell.xPos+1][workingCell.yPos].fillVal == (workingCell.fillVal-1))
		{
			if(previousCell.fillVal != 0)
			{
				if(previousCell.xPos == workingCell.xPos)
				{
					if((previousCell.yPos>workingCell.yPos)&&(turned == 0))
					{
						moveStack.push_back(turnRightMove);
						turned = 1;
					}
					if((previousCell.yPos<workingCell.yPos)&&(turned == 0))
					{
						moveStack.push_back(turnLeftMove);
						turned = 1;
					}
				}
			}
			moveStack.push_back(forwardMove);
			previousCell = workingCell;
			workingCell = MAP[workingCell.xPos+1][workingCell.yPos];
		}
		//next cell South
		if(MAP[workingCell.xPos][workingCell.yPos-1].fillVal == (workingCell.fillVal-1))
		{
			if(previousCell.fillVal != 0)
			{
				if(previousCell.yPos == workingCell.yPos)
				{
					if((previousCell.xPos>workingCell.xPos)&&(turned == 0))
					{
						moveStack.push_back(turnLeftMove);
						turned = 1;
					}
					if((previousCell.xPos<workingCell.xPos)&&(turned == 0))
					{
						moveStack.push_back(turnRightMove);
						turned = 1;
					}
				}
			}
			moveStack.push_back(forwardMove);
			previousCell = workingCell;
			workingCell = MAP[workingCell.xPos][workingCell.yPos-1];
		}
		//next cell South
		if(MAP[workingCell.xPos][workingCell.yPos+1].fillVal == (workingCell.fillVal-1))
		{
			if(previousCell.fillVal != 0)
			{
				if(previousCell.yPos == workingCell.yPos)
				{
					if((previousCell.xPos>workingCell.xPos)&&(turned == 0))
					{
						moveStack.push_back(turnRightMove);
						turned = 1;
					}
					if((previousCell.xPos<workingCell.xPos)&&(turned == 0))
					{
						moveStack.push_back(turnLeftMove);
						turned = 1;
					}
				}
			}
			moveStack.push_back(forwardMove);
			previousCell = workingCell;
			workingCell = MAP[workingCell.xPos][workingCell.yPos+1];
		}
		turned = 0;
	}
}

/***********************************************************************************
Function   :  genStartVector()
Description:  generates the movement steps to get to position (0,0)
Inputs     :  None
Outputs    :  None

Status     :  floodfill done 
***********************************************************************************/
void genStartVector(void)
{
	//reset the fillVals so that past iterations of flood fill dont interfere
	resetFillVals();
	map workingCell, targetCell, previousCell,nextCell;
	bool targetPosFound = 0;
	bool turned = 0;
	
	//load current position as the starting point and set fillVal
	MAP[currentXpos][currentYpos].fillVal = 0x8000;
	floodStack.insert(0,MAP[currentXpos][currentYpos]);
	
	//while the destination hasn't been found yet
	while(targetPosFound == 0)
	{
		//load a cell off the stack 
		workingCell = floodStack.back();
		floodStack.pop_back();
		
		//if the start point of the maze gets filled 
		if(MAP[0][0].fillVal != 0)
		{
			targetPosFound = 1;
		}
		
		//if there is a path from the currently loaded cell and the fill value of that cell is 0, set fillVal and add to stack.
		for(int j = 0;j<4;j++)
		{
			// checks the walls in the order of WEST, SOUTH, EAST, NORTH
			if((workingCell.walls&(0x1<<j)) != (0x1<<j))
			{
				switch(j)
				{
					case(0):
						if(MAP[workingCell.xPos-1][workingCell.yPos].fillVal != 0)
						{
							MAP[workingCell.xPos-1][workingCell.yPos].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos-1][workingCell.yPos]);
						}
					break;
					case(1):
						if(MAP[workingCell.xPos][workingCell.yPos-1].fillVal != 0)
						{
							MAP[workingCell.xPos][workingCell.yPos-1].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos][workingCell.yPos-1]);
						}
					break;
					case(2):
						if(MAP[workingCell.xPos+1][workingCell.yPos].fillVal != 0)
						{
							MAP[workingCell.xPos+1][workingCell.yPos].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos+1][workingCell.yPos]);
						}
					break;
					case(3):
						if(MAP[workingCell.xPos][workingCell.yPos+1].fillVal != 0)
						{
							MAP[workingCell.xPos][workingCell.yPos+1].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos][workingCell.yPos+1]);
						}
					break;
				}
			}	
		}
	}
	//more stuff goes here
}

/***********************************************************************************
Function   :  genRunVector()
Description:  generates the movement steps of the solution to the maze
Inputs     :  None
Outputs    :  None


Status     :  floodfill done
***********************************************************************************/
void genRunVector(void)
{
	//reset the fillVals so that past iterations of flood fill dont interfere
	resetFillVals();
	map workingCell, targetCell, previousCell,nextCell;
	bool targetPosFound = 0;
	bool turned = 0;
	
	//load start position as the starting point and set fillVal
	MAP[0][0].fillVal = 0x8000;
	floodStack.insert(0,MAP[0][0]);
	
	//while the center square hasn't been found yet
	while(targetPosFound == 0)
	{
		//load a cell off the stack 
		workingCell = floodStack.back();
		floodStack.pop_back();
		
		//if the center square has been filled
		if(MAP[8][8].fillVal != 0)
		{
			targetPosFound = 1;
		}
		
		//check walls of working cell. if there is a path, add the adjacent cell to the stack and set its fillVal
		for(int j = 0;j<4;j++)
		{
			// checks the walls in the order of WEST, SOUTH, EAST, NORTH
			if((workingCell.walls&(0x1<<j)) != (0x1<<j))
			{
				switch(j)
				{
					case(0):
						if(MAP[workingCell.xPos-1][workingCell.yPos].fillVal != 0)
						{
							MAP[workingCell.xPos-1][workingCell.yPos].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos-1][workingCell.yPos]);
						}
					break;
					case(1):
						if(MAP[workingCell.xPos][workingCell.yPos-1].fillVal != 0)
						{
							MAP[workingCell.xPos][workingCell.yPos-1].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos][workingCell.yPos-1]);
						}
					break;
					case(2):
						if(MAP[workingCell.xPos+1][workingCell.yPos].fillVal != 0)
						{
							MAP[workingCell.xPos+1][workingCell.yPos].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos+1][workingCell.yPos]);
						}
					break;
					case(3):
						if(MAP[workingCell.xPos][workingCell.yPos+1].fillVal != 0)
						{
							MAP[workingCell.xPos][workingCell.yPos+1].fillVal = (workingCell.fillVal+1);
							floodStack.insert(0,MAP[workingCell.xPos][workingCell.yPos+1]);
						}
					break;
				}
			}	
		}
	}
	//more stuff goes here
}

/***********************************************************************************
Function   :  setNewPos()
Description:  Sets the uMouse's position and direction to the values they will be 
              at the destination of the move
Inputs     :  Movement
Outputs    :  None

Status     :  Complete for current implementation
***********************************************************************************/
void setNewPos(Movement move)
{
	if(move == forward)
	{
		switch (direction)
		{
			case NORTH:
				currentYpos++;
				break;
			case WEST:
				currentXpos++;
				break;
			case SOUTH:
				currentYpos--;
				break;
			case EAST:
				currentXpos--;
				break;
		}
	}
	if(move == turnRight)
	{
		direction++;
		direction %= 0x04;
	}
	if(move == turnLeft)
	{
		direction--;
		direction %= 0x04;
	}
	if(move == turnAround)
	{
		direction++;
		direction++;
		direction %= 0x04;
	}
}

/***********************************************************************************
Function   :  resetEnCount()
Description:  resets the encoder count
Inputs     :  None
Outputs    :  None

Status     :  Complete
***********************************************************************************/
void resetEnCounts()
{
	enCountRight = 0;
	enCountLeft = 0;
}

/***********************************************************************************
Function   :  SystemClock_Config()
Description:  Configures the Base Clock that all other periferals use
Inputs     :  None
Outputs    :  None

Status     :  Probably works? Auto-generated code
***********************************************************************************/
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_MSI;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 16;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_ADC1CLK;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

    /**Configure the main internal regulator output voltage 
    */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/***********************************************************************************
Function   :  ADC1_Init()
Description:  Configures the analog pins and starts the ADC
Inputs     :  None
Outputs    :  None

Status     :  Works!
***********************************************************************************/
static void ADC1_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
  ADC_ChannelConfTypeDef sConfig;
	
	// Enable clock for GPIOA
	__HAL_RCC_GPIOA_CLK_ENABLE();
	
	// Enable ADC Clock
	__HAL_RCC_ADC_CLK_ENABLE();
	
	// ADC Periph interface clock configuration
  __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_SYSCLK);
	
	// Configure GPIOA
	GPIO_InitStruct.Pin   = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 |
	                        GPIO_PIN_4 | GPIO_PIN_5;
	GPIO_InitStruct.Mode  = GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	/* Configure ADC1 */
	hadc1.Instance = ADC1;
	
	// Reset peripheral
	if(HAL_ADC_DeInit(&hadc1)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
	
  // Configure ADC settings
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;       // Scan through all channels based on rank
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.ContinuousConvMode    = ENABLE;
  hadc1.Init.NbrOfConversion       = 5;                     // 5 channels to scan through
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.NbrOfDiscConversion   = 1;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode      = DISABLE;
  
	if(HAL_ADC_Init(&hadc1)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}

  //Configure Channel 5, PA0, IR_BL
  sConfig.Channel      = ADC_CHANNEL_5;
  sConfig.Rank         = ADC_REGULAR_RANK_1;       // scanning will be done in order
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff   = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset       = 0;
  
	if(HAL_ADC_ConfigChannel(&hadc1, &sConfig)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
	
	//Configure Channel 6, PA1, IR_FL
  sConfig.Channel      = ADC_CHANNEL_6;
  sConfig.Rank         = ADC_REGULAR_RANK_2;
  
	if(HAL_ADC_ConfigChannel(&hadc1, &sConfig)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
	
	//Configure Channel 8, PA3, IR_M
  sConfig.Channel      = ADC_CHANNEL_8;
  sConfig.Rank         = ADC_REGULAR_RANK_3;
  
	if(HAL_ADC_ConfigChannel(&hadc1, &sConfig)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
	
	//Configure Channel 9, PA4, IR_FR
  sConfig.Channel      = ADC_CHANNEL_9;
  sConfig.Rank         = ADC_REGULAR_RANK_4;
  
	if(HAL_ADC_ConfigChannel(&hadc1, &sConfig)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
	
	//Configure Channel 10, PA5, IR_BR
  sConfig.Channel      = ADC_CHANNEL_10;
  sConfig.Rank         = ADC_REGULAR_RANK_5;
  
	if(HAL_ADC_ConfigChannel(&hadc1, &sConfig)!=HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
	
	/* Run the ADC calibration in single-ended mode */
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    /* Calibration Error */
    while(1){}
  }
}

/***********************************************************************************
Function   :  TIM1_Init()
Description:  Configure Timer 1 to run 4 PWM outputs for the motors
Inputs     :  None
Outputs    :  None

Status     :  Probably works? Auto-generated code
***********************************************************************************/
static void TIM1_Init(void)
{
  //declares some structs for init purposes
  TIM_ClockConfigTypeDef sClockSourceConfig;
  TIM_OC_InitTypeDef sConfigOC;


	//sets up the base timer 
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 256;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim1);
  
	//sets the reference clock source
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig);

	//enables the PWM functionality
  HAL_TIM_PWM_Init(&htim1);

  //configures the PWM settings and loads it to all 4 channels in use
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3);
  HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4);

	
	//starts the timer and enables the PWM Channels
	HAL_TIM_Base_Start(&htim1); 
	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_1); 
	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_2); 
	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_3); 
	HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_4); 
}

/***********************************************************************************
Function   :  GPIO_Init()
Description:  Configures GPIO pins for input, output, and external interrupt usage
Inputs     :  None
Outputs    :  None

Status     :  Complete, but may be updated with pin configs from other init functions
***********************************************************************************/
static void GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Sets the digital output pins to LOW before enabling them
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2|GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);

	//LED Output pin config
  /*Configure GPIO pins : PA2 PA6 PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	//On Board LED Output pin config
  /*Configure GPIO pins : PB3 */
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	//EXTI input pin config
  /*Configure GPIO pins : PB0 PB1 PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	//Button Digital Input pin config
  /*Configure GPIO pin : PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	//Switch Digital Input pin config
  /*Configure GPIO pins : PB6 PB7 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	
	//PWM pin config
	/*Configure GPIO pins : PA8 PA9 PA10 PA11 */
	//GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
  //GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  //GPIO_InitStruct.Pull = GPIO_NOPULL;
	//GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
	//GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
  
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/***********************************************************************************
Function   :  EXTI_Init()
Description:  Sets Priority and enables the external interupts used for the encoders
Inputs     :  None
Outputs    :  None

Status     :  I made this but don't know if it works
***********************************************************************************/
static void EXTI_Init(void)
{
	HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
	HAL_NVIC_SetPriority(EXTI1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);
	HAL_NVIC_SetPriority(EXTI4_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);
	HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

/***********************************************************************************
Function   :  analogRead()
Description:  Gets the analog values from each of the ADC channels and loads the
              analog value struct
Inputs     :  None
Outputs    :  None

Status     :  Seems to work.  Could test if struct is properly populated.
              How values are stored in the struct should also be looked at.
***********************************************************************************/
static void analogRead(void)
{
	/* Start the conversion process */
  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    /* Start Conversation Error */
    while(1){}
  }
	
	/* Poll for conversions */
	for(int conv_index = 0; conv_index < 5; conv_index++)
	{
		if(HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
		{
			/* loop if error occured*/
			while(1){}
		}
		else
		{
			/* store converted value based on set rank in ADC1_Init */
			switch(conv_index)
			{
				case 0:  analog1.leftBackIRVal = HAL_ADC_GetValue(&hadc1);
					break;
				case 1:  analog1.leftFrontIRVal = HAL_ADC_GetValue(&hadc1);
					break;
				case 2:  analog1.middleIRVal = HAL_ADC_GetValue(&hadc1);
					break;
				case 3:  analog1.rightFrontIRVal = HAL_ADC_GetValue(&hadc1);
					break;
				case 4:  analog1.rightBackIRVal = HAL_ADC_GetValue(&hadc1);
					break;
			}
		}
	}
	
	/* End conversion process */
	if (HAL_ADC_Stop(&hadc1) != HAL_OK)
	{
		/* Error occured */
		while(1){}
	}
}

/***********************************************************************************
Functions  :  EXTI Handlers
Description:  When an external interrupt occurs, run the code listed
Inputs     :  None
Outputs    :  None

Status     :  Complete with the current implementation
***********************************************************************************/
//Encoder Handler for Right A
void EXTI0_IRQHandler(void)
{
  enCountRight++;
	HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_3);//for testing
}

//Encoder Handler for Right B
void EXTI1_IRQHandler(void)
{
  enCountRight++;
	HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_3);//for testing
}

//Encoder Handler for Left A
void EXTI4_IRQHandler(void)
{
  enCountLeft++;
	HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_3);//for testing
}

//Encoder Handler for Left B
void EXTI5_9_IRQHandler(void)
{
  enCountLeft++;
	HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_3);//for testing
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
