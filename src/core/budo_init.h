#ifndef BUDO_INIT_H
#define BUDO_INIT_H

typedef enum BudoInitLanguage
{
    BUDO_INIT_JAVASCRIPT = 0,
    BUDO_INIT_NATIVE_C
} BudoInitLanguage;

typedef enum BudoInitTemplate
{
    BUDO_INIT_TEMPLATE_CANVAS = 0, 
    BUDO_INIT_TEMPLATE_GPU,        
    BUDO_INIT_TEMPLATE_UI          
} BudoInitTemplate;

typedef struct BudoInitOptions
{
    BudoInitLanguage language;
    BudoInitTemplate template_kind;
} BudoInitOptions;

int budo_run_init(const char *target_dir, const BudoInitOptions *options);

#endif