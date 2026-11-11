// CPU/interpreter smoke test only. This is not a DOM or GTK browser test.
const input = Array.from({length: 1000}, (_, index) => index);
if (input.reduce((sum, value) => sum + value, 0) !== 499500)
    throw new Error('arithmetic failed');
const result = JSON.parse('{"name":"Pentium II","count":384}');
if (result.name !== 'Pentium II' || !/Pentium/.test(result.name))
    throw new Error('JSON/string failed');
if (new Intl.NumberFormat('pl-PL').format(12345).length < 5)
    throw new Error('ICU formatting failed');
const buffer = new ArrayBuffer(16);
const view = new DataView(buffer);
view.setFloat64(0, 1.25, true);
if (view.getFloat64(0, true) !== 1.25 || ((1n << 70n) + 3n).toString() !== '1180591620717411303427')
    throw new Error('typed-array/BigInt failed');
print('P2-JSC-CLOOP-PASS');
