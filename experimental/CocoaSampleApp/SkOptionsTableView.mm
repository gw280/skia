#import "SkOptionsTableView.h"
#import "SkTextFieldCell.h"
@implementation SkOptionItem
@synthesize fCell, fItem;
- (void)dealloc {
    [fCell release];
    [super dealloc];
}
@end

@implementation SkOptionsTableView
@synthesize fItems;

- (id)initWithCoder:(NSCoder*)coder {
    if ((self = [super initWithCoder:coder])) {
        self.dataSource = self;
        self.delegate = self;
        [self setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleNone];
        self.fItems = [NSMutableArray array];
    }
    return self;
}

- (void)dealloc {
    self.fItems = nil;
    [super dealloc];
}

- (void) view:(SkNSView*)view didAddMenu:(const SkOSMenu*)menu {}
- (void) view:(SkNSView*)view didUpdateMenu:(const SkOSMenu*)menu {
    [self updateMenu:menu];
}

- (void)registerMenus:(const SkTDArray<SkOSMenu*>*)menus {
    fMenus = menus;
    for (NSUInteger i = 0; i < fMenus->count(); ++i) {
        [self loadMenu:(*fMenus)[i]];
    }
}

- (void)updateMenu:(SkOSMenu*)menu {
    // the first menu is always assumed to be the static, the second is 
    // repopulated every time over and over again 
    int menuIndex = fMenus->find(menu);
    if (menuIndex >= 0 && menuIndex < fMenus->count()) {
        NSUInteger first = 0;
        for (NSInteger i = 0; i < menuIndex; ++i) {
            first += (*fMenus)[i]->countItems();
        }
        [fItems removeObjectsInRange:NSMakeRange(first, [fItems count] - first)];
        [self loadMenu:menu];
    }
    [self reloadData];
}

- (NSCellStateValue)triStateToNSState:(SkOSMenu::TriState)state {
    if (SkOSMenu::kOnState == state)
        return NSOnState;
    else if (SkOSMenu::kOffState == state)
        return NSOffState;
    else
        return NSMixedState;
}

- (void)loadMenu:(const SkOSMenu*)menu {
    for (int i = 0; i < menu->countItems(); ++i) {
        const SkOSMenu::Item* item = menu->getItem(i);
        NSString* str;
        int index = 0;
        NSArray* optionstrs = nil;
        
        SkOptionItem* option = [[SkOptionItem alloc] init];
        option.fItem = item;
        bool state = false;
        SkOSMenu::TriState tristate;
        switch (item->getType()) {
            case SkOSMenu::kAction_Type:
                option.fCell = [self createAction];
                break;                
            case SkOSMenu::kList_Type:
                optionstrs = [[NSString stringWithUTF8String:item->getEvent()->findString(SkOSMenu::List_Items_Str)]
                              componentsSeparatedByString:[NSString stringWithUTF8String:SkOSMenu::Delimiter]];
                item->getEvent()->findS32(item->getSlotName(), &index);
                option.fCell = [self createList:optionstrs current:index];
                break;
            case SkOSMenu::kSlider_Type:
                SkScalar min, max, value;
                item->getEvent()->findScalar(SkOSMenu::Slider_Min_Scalar, &min);
                item->getEvent()->findScalar(SkOSMenu::Slider_Max_Scalar, &max);
                item->getEvent()->findScalar(item->getSlotName(), &value);
                option.fCell = [self createSlider:value 
                                              min:min 
                                              max:max];
                break;                    
            case SkOSMenu::kSwitch_Type:
                item->getEvent()->findBool(item->getSlotName(), &state);
                option.fCell = [self createSwitch:(BOOL)state];
                break;
            case SkOSMenu::kTriState_Type:
                item->getEvent()->findS32(item->getSlotName(), (int*)&tristate);
                option.fCell = [self createTriState:[self triStateToNSState:tristate]];
                break;
            case SkOSMenu::kTextField_Type:
                str = [NSString stringWithUTF8String:item->getEvent()->findString(item->getSlotName())];
                option.fCell = [self createTextField:str];
                break;
            default:
                break;
        }
        [fItems addObject:option];
        [option release];
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return [self.fItems count];
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    int columnIndex = [tableView columnWithIdentifier:[tableColumn identifier]];
    if (columnIndex == 0)
        return [NSString stringWithUTF8String:((SkOptionItem*)[fItems objectAtIndex:row]).fItem->getLabel()];
    else
        return nil;
}

- (NSCell *)tableView:(NSTableView *)tableView dataCellForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    if (tableColumn) {
        int columnIndex = [tableView columnWithIdentifier:[tableColumn identifier]];
        if (columnIndex == 1) 
            return [((SkOptionItem*)[fItems objectAtIndex:row]).fCell copy];
        else
            return [[[SkTextFieldCell alloc] init] autorelease];
    }
    return nil;
}

- (void)tableView:(NSTableView *)tableView willDisplayCell:(id)cell forTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    int columnIndex = [tableView columnWithIdentifier:[tableColumn identifier]];
    if (columnIndex == 1) {
        SkOptionItem* option = (SkOptionItem*)[self.fItems objectAtIndex:row];
        NSCell* storedCell = option.fCell;
        const SkOSMenu::Item* item = option.fItem;
        switch (item->getType()) {
            case SkOSMenu::kAction_Type:
                break;                
            case SkOSMenu::kList_Type:
                [cell selectItemAtIndex:[(NSPopUpButtonCell*)storedCell indexOfSelectedItem]];
                break;
            case SkOSMenu::kSlider_Type:
                [cell setFloatValue:[storedCell floatValue]];
                break;
            case SkOSMenu::kSwitch_Type:
                [cell setTitle:storedCell.title];
                [cell setState:[(NSButtonCell*)storedCell state]];
                break;
            case SkOSMenu::kTextField_Type:
                if ([[storedCell stringValue] length] > 0)
                    [cell setStringValue:[storedCell stringValue]];
                break;
            case SkOSMenu::kTriState_Type:
                [cell setTitle:storedCell.title];
                [cell setState:[(NSButtonCell*)storedCell state]];
                break;
            default:
                break;
        }
    }
    else {
        [(SkTextFieldCell*)cell setEditable:NO];
    }
}

- (void)tableView:(NSTableView *)tableView setObjectValue:(id)anObject forTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    int columnIndex = [tableView columnWithIdentifier:[tableColumn identifier]];
    if (columnIndex == 1) {
        SkOptionItem* option = (SkOptionItem*)[self.fItems objectAtIndex:row];
        NSCell* cell = option.fCell;
        const SkOSMenu::Item* item = option.fItem;
        switch (item->getType()) {
            case SkOSMenu::kAction_Type:
                item->postEvent();
                break;
            case SkOSMenu::kList_Type:
                [(NSPopUpButtonCell*)cell selectItemAtIndex:[anObject intValue]];
                item->postEventWithInt([anObject intValue]);
                break;
            case SkOSMenu::kSlider_Type:
                [cell setFloatValue:[anObject floatValue]];
                item->postEventWithScalar([anObject floatValue]);
                break;
            case SkOSMenu::kSwitch_Type:
                [cell setState:[anObject boolValue]];
                item->postEventWithBool([anObject boolValue]);
                break;
            case SkOSMenu::kTextField_Type:
                if ([anObject length] > 0) {
                    [cell setStringValue:anObject];
                    item->postEventWithString([anObject UTF8String]);
                }
                break;
            case SkOSMenu::kTriState_Type:
                [cell setState:[anObject intValue]];
                item->postEventWithInt([anObject intValue]);
                break;
            default:
                break;
        }
    }
}

- (NSCell*)createAction{
    NSButtonCell* cell = [[[NSButtonCell alloc] init] autorelease];
    [cell setTitle:@""];
    [cell setButtonType:NSMomentaryPushInButton];
    [cell setBezelStyle:NSSmallSquareBezelStyle];
    return cell;
}

- (NSCell*)createList:(NSArray*)items current:(int)index {
    NSPopUpButtonCell* cell = [[[NSPopUpButtonCell alloc] init] autorelease];
    [cell addItemsWithTitles:items];
    [cell selectItemAtIndex:index];
    [cell setArrowPosition:NSPopUpArrowAtBottom];
    [cell setBezelStyle:NSSmallSquareBezelStyle];
    return cell; 
}

- (NSCell*)createSegmented:(NSArray*)items current:(int)index {
    NSSegmentedCell* cell = [[[NSSegmentedCell alloc] init] autorelease];
    [cell setSegmentStyle:NSSegmentStyleSmallSquare];
    [cell setSegmentCount:[items count]];
    NSUInteger i = 0;
    for (NSString* label in items) {
        [cell setLabel:label forSegment:i];
        ++i;
    }
    [cell setSelectedSegment:index];
    return cell; 
}

- (NSCell*)createSlider:(float)value min:(float)min max:(float)max {
    NSSliderCell* cell = [[[NSSliderCell alloc] init] autorelease];
    [cell setFloatValue:value];
    [cell setMinValue:min];
    [cell setMaxValue:max];
    return cell;
}

- (NSCell*)createSwitch:(BOOL)state {
    NSButtonCell* cell = [[[NSButtonCell alloc] init] autorelease];
    [cell setTitle:(state) ? @"On" : @"Off"];
    [cell setState:state];
    [cell setButtonType:NSSwitchButton];
    return cell;
}

- (NSCell*)createTextField:(NSString*)placeHolder; {
    SkTextFieldCell* cell = [[[SkTextFieldCell alloc] init] autorelease];
    [cell setEditable:YES];
    [cell setStringValue:@""];
    [cell setPlaceholderString:placeHolder];
    return cell;
}

- (NSCell*)createTriState:(NSCellStateValue)state {
    NSButtonCell* cell = [[[NSButtonCell alloc] init] autorelease];
    if (NSOnState == state)
        [cell setTitle:@"On"];
    else if (NSOffState == state)
        [cell setTitle:@"Off"];
    else
        [cell setTitle:@"Mixed"];
    [cell setAllowsMixedState:TRUE];
    [cell setState:(NSInteger)state];
    [cell setButtonType:NSSwitchButton];
    return cell;
}
@end
