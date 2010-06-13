#ifndef SMALL
#ifndef NO_STBIMAGE

#include "openiboot.h"
#include "lcd.h"
#include "util.h"
#include "framebuffer.h"
#include "buttons.h"
#include "timer.h"
#include "images/ConsolePNG.h"
#include "images/iPhoneOSPNG.h"
#include "images/AndroidOSPNG.h"
#include "images/ConsoleSelectedPNG.h"
#include "images/iPhoneOSSelectedPNG.h"
#include "images/AndroidOSSelectedPNG.h"
#include "images/HeaderPNG.h"
#include "images.h"
#include "actions.h"
#include "stb_image.h"
#include "pmu.h"
#include "nand.h"
#include "radio.h"
#include "hfs/fs.h"
#include "ftl.h"
#include "scripting.h"
#include "menu.h"
#include "multitouch.h"

int globalFtlHasBeenRestored = 0; /* global variable to tell wether a ftl_restore has been done*/

static uint32_t FBWidth;
static uint32_t FBHeight;

//static uint32_t* imgAndroidOS;
static uint32_t* imgAndroidOS_unblended;
static int imgAndroidOSWidth;
static int imgAndroidOSHeight;
static int imgAndroidOSX;
static int imgAndroidOSY;

//static uint32_t* imgAndroidOSSelected;
static uint32_t* imgAndroidOSSelected_unblended;

static uint32_t* imgHeader;
static int imgHeaderWidth;
static int imgHeaderHeight;
static int imgHeaderX;
static int imgHeaderY;

static MenuTheme *menuTheme;
static int totalDefaultMenuItem=3;

typedef enum MenuSelection {
	MenuSelectioniPhoneOS,
	MenuSelectionAndroidOS,
	MenuSelectionConsole
} MenuSelection;

static int SelectionIndex;

volatile uint32_t* OtherFramebuffer;

static void drawSelectionBox() {
	volatile uint32_t* oldFB = CurFramebuffer;

	CurFramebuffer = OtherFramebuffer;
	currentWindow->framebuffer.buffer = CurFramebuffer;
	OtherFramebuffer = oldFB;
    int ii=0;
    for (ii=0;ii<menuTheme->totalMenu;ii++) {
        if (SelectionIndex == ii) {
            framebuffer_draw_image(menuTheme->menus[ii]->imageFocused.image, menuTheme->menus[ii]->imageFocused.x, menuTheme->menus[ii]->imageFocused.y
                , menuTheme->menus[ii]->imageFocused.w, menuTheme->menus[ii]->imageFocused.h);
        }else {
            framebuffer_draw_image(menuTheme->menus[ii]->imageNormal.image, menuTheme->menus[ii]->imageNormal.x, menuTheme->menus[ii]->imageNormal.y
                , menuTheme->menus[ii]->imageNormal.w, menuTheme->menus[ii]->imageNormal.h);
        }
    }

	lcd_window_address(2, (uint32_t) CurFramebuffer);
}

static int touch_watcher()
{
    multitouch_run();
    int ii=0;
    for (ii=0;ii<menuTheme->totalMenu;ii++) {
        if (multitouch_ispoint_inside_region(menuTheme->menus[ii]->imageNormal.x, menuTheme->menus[ii]->imageNormal.y
            , menuTheme->menus[ii]->imageNormal.w, menuTheme->menus[ii]->imageNormal.h) == TRUE) {
            SelectionIndex = ii;
        drawSelectionBox();
        return TRUE;
        }
    }
    
    return FALSE;
}

static void toggle(int forward) {
    if (forward) {
        SelectionIndex++;
        if (SelectionIndex>=menuTheme->totalMenu) {
            SelectionIndex=0;
        }
    } else {
        SelectionIndex--;
        if (SelectionIndex<0) {
            SelectionIndex=menuTheme->totalMenu-1;
        }
    }

	drawSelectionBox();
}

static inline uint32_t getInt32Field(uint8_t* code) {
    return (code[0]) | (code[1] << 8) | (code[2] << 16) | (code[3] << 24);
}

static int menuThemeSetup() 
{
    //load menutheme.bin
    uint32_t size = fs_extract(1, "/menutheme.bin", (void*) 0x09000000);
    //draw background
    uint8_t *fieldPointer=0;
    
    if (size<1) {
        return FALSE;
    }
    int bgW,bgH;
    
    
    fieldPointer=(void*) 0x09000000;
    menuTheme = (MenuTheme *) malloc(sizeof(MenuTheme));
    menuTheme->backgroundSize = getInt32Field(fieldPointer);
    fieldPointer += 4;
    menuTheme->background = framebuffer_load_image((void*) fieldPointer, menuTheme->backgroundSize, &bgW, &bgH, TRUE);
    fieldPointer += menuTheme->backgroundSize;
    
    framebuffer_draw_image((uint32_t*)menuTheme->background, 0, 0, bgW, bgH);
    
    
    menuTheme->totalMenu = *((uint8_t*) fieldPointer);
    fieldPointer += 1;
    menuTheme->menus = (MenuItem **) malloc(sizeof(MenuItem*) * menuTheme->totalMenu);
    int ii=0;
    
    for (ii=0;ii<menuTheme->totalMenu;ii++) {
        menuTheme->menus[ii] = (MenuItem *) malloc(sizeof(MenuItem));
        
        menuTheme->menus[ii]->osId = *((uint8_t*) fieldPointer); 
        fieldPointer += 1;
        
        menuTheme->menus[ii]->imageNormal.size = getInt32Field(fieldPointer);
        fieldPointer += 4;
        
        menuTheme->menus[ii]->imageNormal.image = framebuffer_load_image((void*) fieldPointer, menuTheme->menus[ii]->imageNormal.size, &(menuTheme->menus[ii]->imageNormal.w), &(menuTheme->menus[ii]->imageNormal.h), TRUE);
        fieldPointer += menuTheme->menus[ii]->imageNormal.size;
        
        menuTheme->menus[ii]->imageNormal.x = *((uint16_t*) fieldPointer); 
        fieldPointer += 2;
        
        menuTheme->menus[ii]->imageNormal.y = *((uint16_t*) fieldPointer); 
        fieldPointer += 2;
        //focused image
        menuTheme->menus[ii]->imageFocused.size = getInt32Field(fieldPointer);
        fieldPointer += 4;
        if (menuTheme->menus[ii]->imageFocused.size > 0)
        {
            menuTheme->menus[ii]->imageFocused.image = framebuffer_load_image((void*) fieldPointer, menuTheme->menus[ii]->imageFocused.size, &(menuTheme->menus[ii]->imageFocused.w), &(menuTheme->menus[ii]->imageFocused.h), TRUE);
            fieldPointer += menuTheme->menus[ii]->imageFocused.size;
            
            menuTheme->menus[ii]->imageFocused.x = *((uint16_t*) fieldPointer);             
            fieldPointer += 2;
            
            menuTheme->menus[ii]->imageFocused.y = *((uint16_t*) fieldPointer);
            fieldPointer += 2;
        }
        
        //bigImage
        menuTheme->menus[ii]->bigImage.size = getInt32Field(fieldPointer);
        fieldPointer += 4;
        if (menuTheme->menus[ii]->bigImage.size > 0) {
            //load bigImage
            menuTheme->menus[ii]->bigImage.image = framebuffer_load_image((void*) fieldPointer, menuTheme->menus[ii]->bigImage.size, &(menuTheme->menus[ii]->bigImage.w), &(menuTheme->menus[ii]->bigImage.h), TRUE);
            fieldPointer += menuTheme->menus[ii]->bigImage.size;
            
            menuTheme->menus[ii]->bigImage.x = *((uint16_t*) fieldPointer);             
            fieldPointer += 2;
            
            menuTheme->menus[ii]->bigImage.y = *((uint16_t*) fieldPointer);
            fieldPointer += 2;
        }
    }
    
    return TRUE;
}

static void menuDefaultSetup() {
    
    menuTheme = (MenuTheme *) malloc(sizeof(MenuTheme));
    menuTheme->totalMenu = totalDefaultMenuItem;
    
    menuTheme->menus = (MenuItem **) malloc(sizeof(MenuItem*) * menuTheme->totalMenu);
        
    //iOs
    menuTheme->menus[0] = (MenuItem *) malloc(sizeof(MenuItem));
    menuTheme->menus[0]->osId = MenuSelectioniPhoneOS;
    menuTheme->menus[0]->imageNormal.size = dataiPhoneOSPNG_size;
    menuTheme->menus[0]->imageNormal.image = framebuffer_load_image(dataiPhoneOSPNG, menuTheme->menus[0]->imageNormal.size, &(menuTheme->menus[0]->imageNormal.w), &(menuTheme->menus[0]->imageNormal.h), TRUE);
    menuTheme->menus[0]->imageNormal.x = (FBWidth - menuTheme->menus[0]->imageNormal.w) / 2; 
    menuTheme->menus[0]->imageNormal.y = 84;
    menuTheme->menus[0]->imageFocused.size = dataiPhoneOSSelectedPNG_size;
    menuTheme->menus[0]->imageFocused.image = framebuffer_load_image(dataiPhoneOSSelectedPNG, menuTheme->menus[0]->imageFocused.size, &(menuTheme->menus[0]->imageFocused.w), &(menuTheme->menus[0]->imageFocused.h), TRUE);
    menuTheme->menus[0]->imageFocused.x = (FBWidth - menuTheme->menus[0]->imageFocused.w) / 2;
    menuTheme->menus[0]->imageFocused.y = 84;
        
    //AndroidOS
    menuTheme->menus[1] = (MenuItem *) malloc(sizeof(MenuItem));
    menuTheme->menus[1]->osId = MenuSelectionAndroidOS;
    menuTheme->menus[1]->imageNormal.size = dataAndroidOSPNG_size;
    //menuTheme->menus[1]->imageNormal.image = framebuffer_load_image(dataAndroidOSPNG, menuTheme->menus[1]->imageNormal.size, &(menuTheme->menus[1]->imageNormal.w), &(menuTheme->menus[1]->imageNormal.h), TRUE);
    imgAndroidOS_unblended = framebuffer_load_image(dataAndroidOSPNG, dataAndroidOSPNG_size, &imgAndroidOSWidth, &imgAndroidOSHeight, TRUE);
    menuTheme->menus[1]->imageNormal.w = imgAndroidOSWidth;
    menuTheme->menus[1]->imageNormal.h = imgAndroidOSHeight;
    menuTheme->menus[1]->imageNormal.x = (FBWidth - menuTheme->menus[1]->imageNormal.w) / 2; 
    menuTheme->menus[1]->imageNormal.y = 207;
    menuTheme->menus[1]->imageFocused.size = dataAndroidOSSelectedPNG_size;
    imgAndroidOSSelected_unblended = framebuffer_load_image(dataAndroidOSSelectedPNG, dataAndroidOSSelectedPNG_size, &imgAndroidOSWidth, &imgAndroidOSHeight, TRUE);
    menuTheme->menus[1]->imageFocused.w = imgAndroidOSWidth;
    menuTheme->menus[1]->imageFocused.h = imgAndroidOSHeight;
    //menuTheme->menus[1]->imageFocused.image = framebuffer_load_image(dataAndroidOSSelectedPNG, menuTheme->menus[1]->imageFocused.size, &(menuTheme->menus[1]->imageFocused.w), &(menuTheme->menus[1]->imageFocused.h), TRUE);
    menuTheme->menus[1]->imageFocused.x = (FBWidth - menuTheme->menus[1]->imageFocused.w) / 2;
    menuTheme->menus[1]->imageFocused.y = 207;
        
    //Console
    menuTheme->menus[2] = (MenuItem *) malloc(sizeof(MenuItem));
    menuTheme->menus[2]->osId = MenuSelectionConsole;
    menuTheme->menus[2]->imageNormal.size = dataConsolePNG_size;
    menuTheme->menus[2]->imageNormal.image = framebuffer_load_image(dataConsolePNG, menuTheme->menus[2]->imageNormal.size, &(menuTheme->menus[2]->imageNormal.w), &(menuTheme->menus[2]->imageNormal.h), TRUE);
    menuTheme->menus[2]->imageNormal.x = (FBWidth - menuTheme->menus[2]->imageNormal.w) / 2; 
    menuTheme->menus[2]->imageNormal.y = 330;
    menuTheme->menus[2]->imageFocused.size = dataConsoleSelectedPNG_size;
    menuTheme->menus[2]->imageFocused.image = framebuffer_load_image(dataConsoleSelectedPNG, menuTheme->menus[2]->imageFocused.size, &(menuTheme->menus[2]->imageFocused.w), &(menuTheme->menus[2]->imageFocused.h), TRUE);
    menuTheme->menus[2]->imageFocused.x = (FBWidth - menuTheme->menus[2]->imageFocused.w) / 2;
    menuTheme->menus[2]->imageFocused.y = 330;
    
    imgHeader = framebuffer_load_image(dataHeaderPNG, dataHeaderPNG_size, &imgHeaderWidth, &imgHeaderHeight, TRUE);
    
    bufferPrintf("menu: images loaded\r\n");
    
    imgAndroidOSX = menuTheme->menus[1]->imageNormal.x;
    imgAndroidOSY = menuTheme->menus[1]->imageNormal.y;
    
    imgHeaderX = (FBWidth - imgHeaderWidth) / 2;
    imgHeaderY = 17;
    
    framebuffer_draw_image(imgHeader, imgHeaderX, imgHeaderY, imgHeaderWidth, imgHeaderHeight);
    
    //framebuffer_draw_rect_hgradient(0, 42, 0, 360, FBWidth, (FBHeight - 12) - 360);
    //framebuffer_draw_rect_hgradient(0x22, 0x22, 0, FBHeight - 12, FBWidth, 12);
    
    menuTheme->menus[1]->imageNormal.image = malloc(imgAndroidOSWidth * imgAndroidOSHeight * sizeof(uint32_t));
    menuTheme->menus[1]->imageFocused.image = malloc(imgAndroidOSWidth * imgAndroidOSHeight * sizeof(uint32_t));
    
    framebuffer_capture_image(menuTheme->menus[1]->imageNormal.image, imgAndroidOSX, imgAndroidOSY, imgAndroidOSWidth, imgAndroidOSHeight);
    framebuffer_capture_image(menuTheme->menus[1]->imageFocused.image, imgAndroidOSX, imgAndroidOSY, imgAndroidOSWidth, imgAndroidOSHeight);
    
    framebuffer_blend_image(menuTheme->menus[1]->imageNormal.image, imgAndroidOSWidth, imgAndroidOSHeight, imgAndroidOS_unblended, imgAndroidOSWidth, imgAndroidOSHeight, 0, 0);
    framebuffer_blend_image(menuTheme->menus[1]->imageFocused.image, imgAndroidOSWidth, imgAndroidOSHeight, imgAndroidOSSelected_unblended, imgAndroidOSWidth, imgAndroidOSHeight, 0, 0);    
}

int menu_setup(int timeout) {
    int isMenuLoaded = FALSE;
    
    #ifndef NO_HFS
        isMenuLoaded = menuThemeSetup();
    #endif
    FBWidth = currentWindow->framebuffer.width;
    FBHeight = currentWindow->framebuffer.height;   
    
    if (isMenuLoaded == FALSE) {
        menuDefaultSetup();
    }
    
    framebuffer_setloc(0, 47);
    framebuffer_setcolors(COLOR_WHITE, 0x222222);
    framebuffer_print_force(OPENIBOOT_VERSION_STR);
    framebuffer_setcolors(COLOR_WHITE, COLOR_BLACK);
    framebuffer_setloc(0, 0);
    
	SelectionIndex=0;

	OtherFramebuffer = CurFramebuffer;
	CurFramebuffer = (volatile uint32_t*) NextFramebuffer;

	drawSelectionBox();

	pmu_set_iboot_stage(0);

	memcpy((void*)NextFramebuffer, (void*) CurFramebuffer, NextFramebuffer - (uint32_t)CurFramebuffer);

	uint64_t startTime = timer_get_system_microtime();
	while(TRUE) {
        if(touch_watcher()) {
            break;
        }
        else
        {
            startTime = timer_get_system_microtime();
            udelay(200000);
        }
		if(buttons_is_pushed(BUTTONS_HOLD)) {
			toggle(TRUE);
			startTime = timer_get_system_microtime();
			udelay(200000);
		}
#ifndef CONFIG_IPOD
		if(!buttons_is_pushed(BUTTONS_VOLUP)) {
			toggle(FALSE);
			startTime = timer_get_system_microtime();
			udelay(200000);
		}
		if(!buttons_is_pushed(BUTTONS_VOLDOWN)) {
			toggle(TRUE);
			startTime = timer_get_system_microtime();
			udelay(200000);
		}
#endif
		if(buttons_is_pushed(BUTTONS_HOME)) {
			break;
		}
		if(timeout > 0 && has_elapsed(startTime, (uint64_t)timeout * 1000)) {
			bufferPrintf("menu: timed out, selecting current item\r\n");
			break;
		}
		udelay(10000);
	}
	int osId = menuTheme->menus[SelectionIndex]->osId;
    if(osId == MenuSelectioniPhoneOS) {
		Image* image = images_get(fourcc("ibox"));
		if(image == NULL)
			image = images_get(fourcc("ibot"));
		void* imageData;
		images_read(image, &imageData);
		chainload((uint32_t)imageData);
	}

    else if(osId == MenuSelectionConsole) {
		// Reset framebuffer back to original if necessary
		if((uint32_t) CurFramebuffer == NextFramebuffer)
		{
			CurFramebuffer = OtherFramebuffer;
			currentWindow->framebuffer.buffer = CurFramebuffer;
			lcd_window_address(2, (uint32_t) CurFramebuffer);
		}

		framebuffer_setdisplaytext(TRUE);
		framebuffer_clear();
	}

    else if(osId == MenuSelectionAndroidOS) {
		// Reset framebuffer back to original if necessary
		if((uint32_t) CurFramebuffer == NextFramebuffer)
		{
			CurFramebuffer = OtherFramebuffer;
			currentWindow->framebuffer.buffer = CurFramebuffer;
			lcd_window_address(2, (uint32_t) CurFramebuffer);
		}

		framebuffer_setdisplaytext(TRUE);
		framebuffer_clear();

#ifndef NO_HFS
#ifndef CONFIG_IPOD
		radio_setup();
#endif
		nand_setup();
		fs_setup();
		if(globalFtlHasBeenRestored) /* if ftl has been restored, sync it, so kernel doesn't have to do a ftl_restore again */
		{
			if(ftl_sync())
			{
				bufferPrintf("ftl synced successfully");
			}
			else
			{
				bufferPrintf("error syncing ftl");
			}
		}

		pmu_set_iboot_stage(0);
		startScripting("linux"); //start script mode if there is a script file
		boot_linux_from_files();
#endif
	}

	return 0;
}

#endif
#endif
