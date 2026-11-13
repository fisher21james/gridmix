#include "i18n.h"
#include <stdio.h>
#include <string.h>

namespace gridcore {

static Lang lang = LANG_EN;
void set_lang(Lang l) { lang = l; }
Lang current_lang() { return lang; }
Lang lang_from_code(const char *code) { return code && !strcmp(code, "fr") ? LANG_FR : LANG_EN; }

struct Pair { const char *en, *fr; };

// Every English string shown on the screen, with its French. Keep the % codes identical.
// Lengths matter: the pill and the chips are narrow, so the French is kept as short as the English allows.
static const Pair FR[] = {
  // fuels
  {"Solar", "Solaire"}, {"Wind", "\xC3\x89olien"}, {"Hydro", "Hydraulique"}, {"Battery", "Batteries"},
  {"Nuclear", "Nucl\xC3\xA9" "aire"}, {"Gas", "Gaz"}, {"Coal", "Charbon"}, {"Other", "Autre"},
  {"Biomass", "Bio\xC3\xA9nergies"}, {"Imports", "Importations"},

  // main screen
  {"Your rate", "Votre tarif"},
  {"Add your power plan at gridmix.local", "Ajoutez votre tarif sur gridmix.local"},
  {"renewable right now", "renouvelable"},
  {"Details \xE2\x80\xBA", "D\xC3\xA9tails \xE2\x80\xBA"},
  {"Show the grid for", "Afficher le r\xC3\xA9seau"},
  {"My region \xC2\xB7 %s", "Ma r\xC3\xA9gion \xC2\xB7 %s"},
  {"Waiting for data", "En attente des donn\xC3\xA9" "es"},
  {"Waiting for data\xE2\x80\xA6", "En attente des donn\xC3\xA9" "es\xE2\x80\xA6"},
  {"Saved before restart", "Gard\xC3\xA9" "es avant red\xC3\xA9marrage"},
  {"Sample data \xC2\xB7 connect Wi-Fi with the installer", "Exemple \xC2\xB7 connectez le Wi-Fi avec l'installateur"},
  {"Sample data \xC2\xB7 finish setup at gridmix.local", "Exemple \xC2\xB7 terminez sur gridmix.local"},
  {"No Wi-Fi \xC2\xB7 reconnect with the installer", "Pas de Wi-Fi \xC2\xB7 reconnectez avec l'installateur"},
  {"Data key not accepted \xC2\xB7 check gridmix.local", "Cl\xC3\xA9 refus\xC3\xA9" "e \xC2\xB7 v\xC3\xA9rifiez gridmix.local"},
  {"Out of data requests for today", "Plus de requ\xC3\xAAtes pour aujourd'hui"},
  {"Can't reach the data service \xC2\xB7 retrying", "Service de donn\xC3\xA9" "es injoignable \xC2\xB7 nouvel essai"},
  {"Every 15 min", "Toutes les 15 min"},
  {"Whole country", "Tout le pays"},
  {"national grid", "r\xC3\xA9seau national"},

  // freshness
  {"Updated just now", "Mis \xC3\xA0 jour \xC3\xA0 l'instant"},
  {"Updated %d min ago", "Mis \xC3\xA0 jour il y a %d min"},
  {"Updated %d h ago", "Mis \xC3\xA0 jour il y a %d h"},
  {"Updated %d day ago", "Mis \xC3\xA0 jour il y a %d jour"},
  {"Updated %d days ago", "Mis \xC3\xA0 jour il y a %d jours"},

  // advice pill
  {"coal & gas", "charbon et gaz"}, {"coal", "charbon"}, {"gas", "gaz"},
  {"Mostly renewable", "Surtout renouvelable"},
  {"Mostly %s", "Surtout %s"},
  {"Few renewables", "Peu de renouvelables"},
  {"Part renewable", "En partie renouvelable"},
  {"%s%sprice goes up at %s", "%s%sle prix monte \xC3\xA0 %s"},
  {"%s, but your rate is high", "%s, mais tarif \xC3\xA9lev\xC3\xA9"},
  {"%s%speak rate, hold off if you can", "%s%starif \xC3\xA9lev\xC3\xA9, attendez si possible"},
  {"%s, but your rate is cheap", "%s, mais tarif bas"},
  {"%s%scheap rate, good for big loads", "%s%starif bas, lancez les machines"},
  {"%s right now", "%s en ce moment"},
  {"Mix of renewables & fossil fuels", "Renouvelables et fossiles"},
  {"Mix of renewables & other sources", "Renouvelables et autres sources"},

  // detail screen
  {"\xE2\x80\xB9 Back", "\xE2\x80\xB9 Retour"},
  {"Mix now", "Mix actuel"},
  {"Renewables \xC2\xB7 last 24 h", "Renouvelables \xC2\xB7 24 derni\xC3\xA8res heures"},
  {"Builds up over the first day", "Se remplit pendant le premier jour"},
  {"24 h ago", "il y a 24 h"},
  {"now", "maintenant"},
  {"Cheapest time for big loads", "Cr\xC3\xA9neau le moins cher"},
  {"%d%% renewable", "%d%% renouvelable"},
  {"incl. rooftop (est.)", "toits inclus (est.)"},
  {"some charging", "en partie en charge"},
  {"Total %s MW", "Total %s MW"},
  {" \xC2\xB7 batteries charging %s MW", " \xC2\xB7 batteries en charge %s MW"},
  {"%sRenewable %% includes biomass", "%s%% renouvelable : bio\xC3\xA9nergies incluses"},
  {"Add your plan at gridmix.local", "Ajoutez votre tarif sur gridmix.local"},
  {"Any time (flat rate)", "\xC3\x80 tout moment (tarif unique)"},

  // tariff screen
  {"Your tariff", "Votre tarif"},
  {"No power plan yet", "Pas encore de tarif"},
  {"Add your rates at gridmix.local from your phone", "Ajoutez vos tarifs sur gridmix.local depuis votre t\xC3\xA9l\xC3\xA9phone"},
  {"Today", "Aujourd'hui"},
  {"Edit at #4FB3D9 gridmix.local#", "Modifier sur #4FB3D9 gridmix.local#"},
  {"Flat rate", "Tarif unique"},
  {"Time of use", "Heures pleines / creuses"},
  {"Now %s \xC2\xB7 #FFFFFF %s/kWh#", "Maintenant %s \xC2\xB7 #FFFFFF %s/kWh#"},
  {"No rate set for this time \xC2\xB7 check your plan at gridmix.local", "Aucun tarif \xC3\xA0 cette heure \xC2\xB7 v\xC3\xA9rifiez sur gridmix.local"},
  {"Same rate all day \xC2\xB7 %s/kWh", "M\xC3\xAAme tarif toute la journ\xC3\xA9" "e \xC2\xB7 %s/kWh"},
  {"%s starts at %s \xC2\xB7 %s/kWh, %s", "%s \xC3\xA0 partir de %s \xC2\xB7 %s/kWh, %s"},
  {"run big loads before then", "lancez les machines avant"},
  {"wait for it if you can", "attendez si possible"},
  {"Supply %s%s/day \xC2\xB7 Feed-in %s%s/kWh", "Abonnement %s%s/jour \xC2\xB7 Revente %s%s/kWh"},
  {"All day", "Toute la journ\xC3\xA9" "e"},
  {"Now", "Maintenant"},
  {"Tomorrow ", "Demain "},
  {"12am", "0h"}, {"6am", "6h"}, {"12pm", "12h"}, {"6pm", "18h"},

  // software update
  {"Updating Grid Mix", "Mise \xC3\xA0 jour de Grid Mix"},
  {"Keep it plugged in. It restarts by itself when done.", "Laissez-le branch\xC3\xA9. Il red\xC3\xA9marre tout seul \xC3\xA0 la fin."},
  {"Done. Restarting\xE2\x80\xA6", "Termin\xC3\xA9. Red\xC3\xA9marrage\xE2\x80\xA6"},
  {"Downloading the new version\xE2\x80\xA6", "T\xC3\xA9l\xC3\xA9" "chargement de la nouvelle version\xE2\x80\xA6"},
};

const char *T(const char *en) {
  if (lang == LANG_EN || !en) return en;
  for (const Pair &p : FR)
    if (!strcmp(p.en, en)) return p.fr;
  return en;
}

void format_decimal(float v, int decimals, char *out, size_t cap) {
  snprintf(out, cap, "%.*f", decimals, v);
  if (lang == LANG_FR)
    for (char *p = out; *p; p++) if (*p == '.') *p = ',';
}

void format_thousands(long n, char *out, size_t cap) {
  const char *sep = lang == LANG_FR ? " " : ",";
  if (n >= 1000000) snprintf(out, cap, "%ld%s%03ld%s%03ld", n / 1000000, sep, n / 1000 % 1000, sep, n % 1000);
  else if (n >= 1000) snprintf(out, cap, "%ld%s%03ld", n / 1000, sep, n % 1000);
  else snprintf(out, cap, "%ld", n);
}

void format_header_clock(int minute_of_day, char *out, size_t cap) {
  const int md = ((minute_of_day % 1440) + 1440) % 1440;
  const int h = md / 60, m = md % 60;
  if (lang == LANG_FR) { snprintf(out, cap, "%d:%02d", h, m); return; }
  snprintf(out, cap, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "am" : "pm");
}

}  // namespace gridcore
