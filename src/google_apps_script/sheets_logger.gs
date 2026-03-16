function doGet(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();

  if (sheet.getLastRow() === 0) {
    sheet.appendRow(['Время', 'Влажность (%)', 'Насос', 'Режим', 'Порог (%)']);
    sheet.getRange(1, 1, 1, 5).setFontWeight('bold');
    sheet.setFrozenRows(1);
  }

  var moisture = parseInt(e.parameter.moisture) || 0;
  var pump = e.parameter.pump || '';
  var mode = e.parameter.mode || '';
  var threshold = parseInt(e.parameter.threshold) || 0;

  sheet.appendRow([new Date(), moisture, pump, mode, threshold]);

  return ContentService.createTextOutput('OK');
}
