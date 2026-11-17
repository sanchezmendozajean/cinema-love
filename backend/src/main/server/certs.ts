import { chmodSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import forge from 'node-forge'

export interface Certificado {
  /** PEM del certificado. */
  cert: string
  /** PEM de la clave privada. */
  key: string
  /** Huella SHA-256 del certificado (DER) en hexadecimal con `:`, p. ej. `AB:CD:…`. */
  huella: string
}

const DIEZ_ANIOS_MS = 10 * 365 * 24 * 3600 * 1000

/** Huella SHA-256 (la que se compara a ojo entre el servidor y el cliente). */
export function huellaDePem(certPem: string): string {
  const der = forge.asn1.toDer(forge.pki.certificateToAsn1(forge.pki.certificateFromPem(certPem))).getBytes()
  const hex = forge.md.sha256.create().update(der).digest().toHex().toUpperCase()
  return hex.match(/.{2}/g)!.join(':')
}

/** Normaliza una huella escrita a mano (con o sin `:`, mayúsculas o minúsculas) para compararla. */
export const normalizarHuella = (h: string): string => h.replace(/[^0-9a-f]/gi, '').toUpperCase()

/**
 * Certificado autofirmado de 10 años con SAN para `localhost`, el nombre del equipo y las IP de la LAN. Los clientes no lo
 * validan por cadena de confianza sino por huella (`Confío en este certificado`), así que los SAN son solo cortesía.
 */
export function generarCertificado(o: { nombre: string; ips: string[] }): Certificado {
  const claves = forge.pki.rsa.generateKeyPair({ bits: 2048, e: 0x10001 })
  const cert = forge.pki.createCertificate()
  cert.publicKey = claves.publicKey
  // Serie positiva y aleatoria (el primer byte < 0x80 evita que ASN.1 la lea como negativa).
  cert.serialNumber = `01${forge.util.bytesToHex(forge.random.getBytesSync(15))}`
  cert.validity.notBefore = new Date(Date.now() - 24 * 3600 * 1000)
  cert.validity.notAfter = new Date(Date.now() + DIEZ_ANIOS_MS)
  const atributos = [
    { name: 'commonName', value: 'CinemaLove' },
    { name: 'organizationName', value: 'CinemaLove' }
  ]
  cert.setSubject(atributos)
  cert.setIssuer(atributos)
  const dns = [...new Set(['localhost', o.nombre].filter((n) => /^[A-Za-z0-9.-]+$/.test(n)))]
  cert.setExtensions([
    { name: 'basicConstraints', cA: false },
    { name: 'keyUsage', digitalSignature: true, keyEncipherment: true },
    { name: 'extKeyUsage', serverAuth: true },
    { name: 'subjectAltName', altNames: [...dns.map((value) => ({ type: 2, value })), ...[...new Set(['127.0.0.1', ...o.ips])].map((ip) => ({ type: 7, ip }))] }
  ])
  cert.sign(claves.privateKey, forge.md.sha256.create())
  const pem = forge.pki.certificateToPem(cert)
  return { cert: pem, key: forge.pki.privateKeyToPem(claves.privateKey), huella: huellaDePem(pem) }
}

/** Certificado persistido en `<datos>/certs/`: se reutiliza entre arranques para que los clientes no tengan que volver a confiar. */
export class AlmacenCertificados {
  constructor(private readonly carpeta: string) {}

  private get archivoCert(): string {
    return join(this.carpeta, 'certs', 'server.crt')
  }
  private get archivoKey(): string {
    return join(this.carpeta, 'certs', 'server.key')
  }

  existe(): boolean {
    return existsSync(this.archivoCert) && existsSync(this.archivoKey)
  }

  cargar(): Certificado | null {
    if (!this.existe()) return null
    try {
      const cert = readFileSync(this.archivoCert, 'utf8')
      return { cert, key: readFileSync(this.archivoKey, 'utf8'), huella: huellaDePem(cert) }
    } catch {
      return null // archivo corrupto: se regenerará
    }
  }

  guardar(c: Certificado): void {
    mkdirSync(join(this.carpeta, 'certs'), { recursive: true })
    writeFileSync(this.archivoCert, c.cert)
    writeFileSync(this.archivoKey, c.key)
    try {
      chmodSync(this.archivoKey, 0o600)
    } catch {
      /* Windows: los permisos POSIX no aplican */
    }
  }

  cargarOGenerar(datos: { nombre: string; ips: string[] }): Certificado {
    const existente = this.cargar()
    if (existente) return existente
    const nuevo = generarCertificado(datos)
    this.guardar(nuevo)
    return nuevo
  }

  /** Genera uno nuevo y lo guarda: los clientes tendrán que volver a confiar. */
  regenerar(datos: { nombre: string; ips: string[] }): Certificado {
    const nuevo = generarCertificado(datos)
    this.guardar(nuevo)
    return nuevo
  }
}
