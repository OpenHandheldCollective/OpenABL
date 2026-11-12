/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
 Fr - Les chaînes du menu en français.
*/

#include "Ui/Lang.h"

CONST LANG_STRINGS gLangFr = {
  .Name              = "Français",
  .Language          = "Langue",
  .LanguageExplain   = "Langue d'affichage du menu.",
  .SectionBoot       = "Démarrage",
  .SectionSettings   = "Paramètres",
  .SectionSystem     = "Système",
  .Start             = "Démarrer",
  .StartLinux        = "Démarrer Linux",
  .StartAndroid      = "Démarrer Android",
  .BootEntries       = "Entrées de démarrage",
  .DefaultFile       = "Chargeur EFI",
  .RestartBootloader = "Redémarrer le chargeur d'amorçage",
  .RecoveryMode      = "Mode récupération",
  .Edl               = "Mode de téléchargement d'urgence (EDL)",
  .OtherSlot         = "Démarrer un autre emplacement",
  .BootMode          = "Mode de démarrage",
  .AblMode           = "Mode ABL",
  .Verbose           = "Détaillé",
  .Silent            = "Silencieux",
  .VolumeUp          = "Volume + au démarrage",
  .VolLinuxExternal  = "Linux (externe)",
  .VolRecovery       = "Récupération",
  .BootSource        = "Source de démarrage",
  .Auto              = "Auto",
  .Internal          = "Interne",
  .SdCard            = "Carte SD",
  .Charging          = "Charge appareil éteint",
  .On                = "Activé",
  .Off               = "Désactivé",
  .Rotation          = "Rotation de l'écran",
  .ReverseVolume     = "Inverser les boutons de volume",
  .CustomLogo        = "Logo de démarrage personnalisé",
  .RotationAuto      = "Auto ({}°)",
  .DeviceModel       = "Modèle de l'appareil",
  .NotSet            = "Non défini",
  .UninstallCfw      = "Désinstaller le CFW",
  .PowerOff          = "Éteindre",
  .ToastTargetExited = "La cible de démarrage s'est arrêtée",
  .ToastAndroidInAbl = "Android est sélectionné dans le menu de l'ABL",
  .ToastFirmwareDt   = "Utiliser l'arborescence matérielle du firmware",
  .ToastModelSet     = "Modèle de l'appareil défini sur {}",
  .LookingOn         = "Recherche de Linux sur {}",
  .SrcFirstDrive     = "le premier lecteur amorçable",
  .SrcInternal       = "stockage interne",
  .SrcSd             = "la carte SD",
  .Countdown         = "Démarrage de {0} dans {1} s",
  .AnyKeyToStay      = "Appuyez sur une touche pour rester",
  .InfoRam           = "Mémoire vive",
  .InfoStorage       = "Stockage",
  .InfoDisplay       = "Affichage",
  .InfoModel         = "Modèle",
  .InfoPanel         = "Écran",
  .DeviceInfo        = "Informations sur l'appareil",
  .Log               = "Journal",

  .DeveloperOptions              = "Options pour les développeurs",
  .DeveloperOptionsUsbLive       = "Hôte USB actif",
  .DeveloperOptionsQuiesce       = "Désactiver l'écran",
  .DeveloperOptionsPanelLines    = "Désactiver les lignes de l'écran",
  .DeveloperOptionsBootLog       = "Journal de démarrage à l'écran",
  .DeveloperOptionsSdDiagnostics = "Diagnostic de la carte SD",
  .DeveloperOptionsReset         = "Réinitialiser les paramètres OpenABL",
  .DeveloperOptionsResetFailed   =
    "Les paramètres n'ont pas pu être réinitialisés",
  .DeveloperOptionsStepsAway     =
    "Plus que {} étapes pour débloquer les options pour les développeurs",
  .DeveloperOptionsStepAway      =
    "Plus qu'une étape pour débloquer les options pour les développeurs",
  .DeveloperOptionsUnlocked      =
    "Les options pour les développeurs sont débloquées !",

  .InfoChip          = "Puce",
  .InfoPlatform      = "Plateforme",
  .InfoPmic          = "PMIC",
  .InfoStorageChip   = "Puce de stockage",
  .InfoAndroid       = "Version d'Android",
  .InfoBootloader    = "Chargeur de démarrage",
  .InfoUefi          = "Firmware UEFI",
  .HintMove          = "Haut et bas",
  .HintSelect        = "Sélectionner",
  .HintBack          = "Retour",
  .NoDeviceTrees     = "Aucune arborescence matérielle trouvée",
  .OneModel          = "1 modèle trouvé",
  .NModels           = "{} modèles trouvés",
  .Current           = "Actuel",
  .FirmwareDefault   = "Valeurs par défaut du firmware",
  .Back              = "Retour",
  .ModelExplain = "Le modèle détermine l'arborescence matérielle utilisée par "
                  "Linux au démarrage ainsi que la rotation de l'écran.",
  .FirmwareDtExplain =
    "Démarrer avec l'arborescence matérielle fournie par le firmware, sans "
    "aucune modification.",
  .FoundIn        = " Trouvé dans {}.",
  .EntriesExplain = "Sélectionnez une entrée pour la démarrer. Le démarrage "
                    "essaiera les entrées dans cet ordre.",
  .NoTriesLeft    = "Aucune tentative restante",
  .Default        = "Par défaut",
  .BackToMenu     = "Retour au menu.",
  .LookingForDts  = "Recherche des arborescences matérielles",
  .FewSeconds     = "Cela peut prendre quelques secondes.",
  .StartingAndroid = "Démarrage d'Android",
  .StartingLinux   = "Démarrage de Linux",
  .Starting        = "Démarrage de {}",
  .ErrNoImageTitle = "Impossible d'amorcer Linux",
  .ErrNoImageMsg   = "Aucune image amorçable n'a été trouvée sur la source "
                     "sélectionnée.",
  .ErrNoImageHint  = "Vérifiez le lecteur ou modifiez la source de démarrage "
                     "dans le menu.",
  .ErrNoModelMsg   = "Aucun modèle d'appareil n'est défini.",
  .ErrLoadTitle    = "Impossible de démarrer Linux",
  .ErrLoadHint =
    "Vérifiez les fichiers d'amorçage sur le lecteur ou modifiez la source "
    "de démarrage dans le menu.",
  .ErrAndroidTitle       = "Démarrage d'Android indisponible",
  .ErrAndroidMsg         = "Cette version ne permet pas encore de démarrer "
                           "Android sur cet appareil.",
  .ErrAndroidHint        = "Sélectionnez Linux comme mode de démarrage dans "
                           "le menu.",
  .ErrAndroidFailedTitle = "Impossible de lancer Android",
  .ErrAndroidFailedMsg   = "Les images de démarrage d'Android n'ont pas pu "
                           "être lancées.",
  .ErrAndroidFailedHint  = "Consultez le journal pour connaître la raison. "
                           "Vérifiez les partitions Android ou sélectionnez "
                           "Linux comme mode de démarrage.",
  .PressAnyKeyMenu       = "Appuyez sur une touche pour revenir au menu",
  .PressAnyKeyModel      = "Appuyez sur une touche pour choisir le modèle "
                           "de l'appareil",
  .ConfirmUninstallTitle = "Désinstaller le CFW ?",
  .ConfirmUninstallMsg   = "Les partitions suivantes seront effacées, ainsi "
                           "que toutes les données Android qu'elles "
                           "contiennent :",
  .ConfirmUninstall      = "Désinstaller",
  .ConfirmUninstallFinalTitle = "Effacer définitivement ?",
  .ConfirmUninstallFinalMsg =
    "L'appareil redémarrera en mode récupération, qui effacera les données "
    "Android. Cette opération est irréversible.",
  .ConfirmUninstallFinal = "Effacer",
  .ConfirmEdlTitle       = "Passer en mode de téléchargement d'urgence ?",
  .ConfirmEdlMsg = "L'écran restera éteint dans ce mode. Vous aurez besoin "
                   "d'un ordinateur équipé des outils de flashage Qualcomm "
                   "pour en sortir.",
  .ConfirmEdl    = "Passer en mode EDL",
  .Cancel        = "Annuler",
  .AblSettingFailed    = "L'ABL n'a pas pu modifier ce paramètre",
  .AblModelFailed      = "L'ABL n'a pas pu enregistrer le modèle de l'appareil",
  .AblNotAnswering     = "L'ABL ne répond plus",
  .AblRestartFailed    = "Échec du redémarrage de l'ABL ({})",
  .RestartingToScan    = "Redémarrage pour lancer la recherche",
  .RestartToScanFailed = "Impossible de redémarrer pour lancer la recherche",
  .ModelScanFailed     = "Impossible de rechercher les modèles d'appareil",
  .AblActionFailed     = "L'ABL n'a pas pu effectuer cette opération",
  .PowerOffFailed      = "Impossible d'éteindre l'appareil",
  .RestartingRecovery  = "Redémarrage en mode récupération",
  .CfwRemoved         = "Linux a été supprimé. Le mode récupération va "
                        "maintenant effacer les données Android.",
  .WipeInRecovery     = "Linux a été supprimé. Sélectionnez « Réinitialisation "
                        "d'usine » dans le mode récupération pour effacer "
                        "les données Android.",
  .UninstallFailed    = "Échec de la désinstallation",
  .Uninstalling       = "Désinstallation du CFW",
  .DontTurnOff        = "N'éteignez pas l'appareil.",
  .UninstallNotFound  = "Linux n'est plus présent sur le stockage interne.",
  .UninstallUntrusted = "La table de partitions ne peut pas être considérée "
                        "comme fiable. Aucune modification n'a été effectuée.",
  .UninstallWriteFailed = "Échec de l'écriture de la table de partitions ({}).",
  .UninstallReadBack    = "La table de partitions relue ne correspond pas "
                          "à celle qui a été écrite.",
  .UninstallError       = "Une erreur s'est produite ({}).",
  .ErrWipeTitle         = "Linux a été supprimé",
  .WipeByHand       = "Android n'a pas pu être défini comme mode de démarrage. "
                      "Sélectionnez Android comme mode de démarrage, puis "
                      "choisissez « Réinitialisation d'usine » dans le mode "
                      "récupération.",
  .Unknown          = "Inconnu",
  .NotInserted      = "Non inséré",
  .SrcAblScan       = "la recherche de l'ABL",
  .EntryType1       = "Entrée BLS",
  .EntryUki         = "Image noyau unifiée (UKI)",
  .InternalStorage  = "Stockage interne",
  .RemovableStorage = "Stockage amovible",
};