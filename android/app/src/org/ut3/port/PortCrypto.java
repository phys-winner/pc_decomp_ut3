package org.ut3.port;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Win32 protected-data compatibility. Secret keys stay in this app UID's
 * AndroidKeyStore; the original game remains responsible for key validation. */
public final class PortCrypto {
    private static final int MAX_BLOB = 1 << 20;
    private static final int MAX_DESCRIPTION = 8192;
    private static final String ALIAS = "org.ut3.port.dpapi.v1";
    private static final byte[] MAGIC = {'U','T','3','D','P',1,0,0};
    private static final byte[] ID_MAGIC = {'U','T','3','I','D',0,0,1};
    private static final byte[] MIGRATION = {'U','T','3','M','I','G','0','1'};
    private static final byte[] DOMAIN = "org.ut3.port.CryptProtectData.v1\0".getBytes(StandardCharsets.US_ASCII);
    private static byte[] adapterMac;
    private static String identityRoot;
    private static SecretKey cachedKey;

    private PortCrypto() {}

    /** Called before nativeStart. This is a random virtual adapter identity,
     * never a physical MAC. Keeping it stable also keeps guest entropy stable. */
    public static synchronized void init(File root) throws IOException {
        if (root == null || (!root.isDirectory() && !root.mkdirs())) throw new IOException("Port identity unavailable");
        String canonical = root.getCanonicalPath();
        if (adapterMac != null) {
            if (!canonical.equals(identityRoot)) throw new IOException("Port identity root changed");
            return;
        }
        AtomicFile identity = new AtomicFile(new File(root, "port-adapter-v1.bin"));
        try (RandomAccessFile lockFile = new RandomAccessFile(new File(root, "port-adapter-v1.lock"), "rw");
             FileLock lock = lockFile.getChannel().lock()) {
            byte[] stored = new byte[14];
            if (identity.getBaseFile().exists() || new File(identity.getBaseFile().getPath() + ".bak").exists()) {
                try (DataInputStream input = new DataInputStream(identity.openRead())) {
                    input.readFully(stored);
                    if (input.read() != -1) throw new IOException("Invalid port identity");
                }
                if (!Arrays.equals(Arrays.copyOf(stored, 8), ID_MAGIC) || (stored[8] & 3) != 2)
                    throw new IOException("Invalid port identity");
            } else {
                System.arraycopy(ID_MAGIC, 0, stored, 0, 8);
                byte[] random = new byte[6]; new SecureRandom().nextBytes(random);
                random[0] = (byte)((random[0] & 0xfc) | 2);
                System.arraycopy(random, 0, stored, 8, 6); Arrays.fill(random, (byte)0);
                FileOutputStream stream = null;
                try {
                    stream = identity.startWrite(); stream.write(stored); stream.getFD().sync();
                    identity.finishWrite(stream); stream = null;
                } finally { if (stream != null) identity.failWrite(stream); }
                try (DataInputStream check = new DataInputStream(identity.openRead())) {
                    byte[] committed = new byte[14]; check.readFully(committed);
                    if (check.read() != -1 || !Arrays.equals(committed, stored)) throw new IOException("Port identity write failed");
                    Arrays.fill(committed, (byte)0);
                }
            }
            adapterMac = Arrays.copyOfRange(stored, 8, 14); identityRoot = canonical;
            Arrays.fill(stored, (byte)0);
        }
    }

    public static synchronized String getAdapterMacHex() {
        if (adapterMac == null) throw new IllegalStateException("Port identity unavailable");
        return hex(adapterMac);
    }

    private static synchronized SecretKey key(boolean create) throws GeneralSecurityException, IOException {
        if (cachedKey != null) return cachedKey;
        KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null);
        if (!store.containsAlias(ALIAS)) {
            if (!create) throw new GeneralSecurityException("Protected-data key unavailable");
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setRandomizedEncryptionRequired(true).build());
            cachedKey = generator.generateKey();
        } else {
            java.security.Key value = store.getKey(ALIAS, null);
            if (!(value instanceof SecretKey)) throw new GeneralSecurityException("Protected-data key unavailable");
            cachedKey = (SecretKey)value;
        }
        return cachedKey;
    }

    private static void aad(Cipher cipher, byte[] entropy) {
        cipher.updateAAD(DOMAIN); cipher.updateAAD(MAGIC);
        cipher.updateAAD(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(entropy.length).array());
        cipher.updateAAD(entropy);
    }

    /** Only copied byte arrays cross JNI. Always returns {output, UTF16LE description}.
     * Inputs are consumed and erased; callers needing them again must clone them. */
    public static byte[][] transform(boolean protect, byte[] input, byte[] entropy, byte[] description)
            throws GeneralSecurityException, IOException {
        byte[] payload = null;
        try {
            if (input == null || entropy == null || description == null || input.length > MAX_BLOB ||
                    entropy.length > MAX_BLOB || description.length > MAX_DESCRIPTION || (description.length & 1) != 0)
                throw new GeneralSecurityException("Invalid protected data");
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            if (protect) {
                if (input.length > MAX_BLOB - 40 - description.length) throw new GeneralSecurityException("Invalid protected data");
                payload = ByteBuffer.allocate(4 + description.length + input.length).order(ByteOrder.LITTLE_ENDIAN)
                        .putInt(description.length).put(description).put(input).array();
                cipher.init(Cipher.ENCRYPT_MODE, key(true));
                byte[] iv = cipher.getIV();
                if (iv == null || iv.length != 12) throw new GeneralSecurityException("Protected-data nonce unavailable");
                aad(cipher, entropy); byte[] encrypted = cipher.doFinal(payload);
                byte[] output = ByteBuffer.allocate(MAGIC.length + iv.length + encrypted.length)
                        .put(MAGIC).put(iv).put(encrypted).array();
                return new byte[][]{output, new byte[0]};
            }
            if (input.length < 40 || !Arrays.equals(Arrays.copyOf(input, MAGIC.length), MAGIC))
                throw new GeneralSecurityException("Invalid protected data");
            cipher.init(Cipher.DECRYPT_MODE, key(false), new GCMParameterSpec(128, input, MAGIC.length, 12));
            aad(cipher, entropy); payload = cipher.doFinal(input, MAGIC.length + 12, input.length - MAGIC.length - 12);
            if (payload.length < 4) throw new GeneralSecurityException("Invalid protected data");
            int count = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN).getInt();
            if (count < 0 || count > MAX_DESCRIPTION || (count & 1) != 0 || count > payload.length - 4)
                throw new GeneralSecurityException("Invalid protected data");
            return new byte[][]{Arrays.copyOfRange(payload, 4 + count, payload.length), Arrays.copyOfRange(payload, 4, 4 + count)};
        } finally {
            if (payload != null) Arrays.fill(payload, (byte)0);
            if (input != null) Arrays.fill(input, (byte)0);
            if (entropy != null) Arrays.fill(entropy, (byte)0);
            if (description != null) Arrays.fill(description, (byte)0);
        }
    }

    private static String hex(byte[] input) {
        char[] output = new char[input.length * 2]; char[] digits = "0123456789abcdef".toCharArray();
        for (int i = 0; i < input.length; ++i) { output[2*i] = digits[(input[i] & 255) >>> 4]; output[2*i+1] = digits[input[i] & 15]; }
        return new String(output);
    }

    private static int littleInt(DataInputStream input) throws IOException {
        byte[] raw = new byte[4]; input.readFully(raw);
        return ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).getInt();
    }

    private static void cliProvider() throws Exception {
        if (android.os.Process.myUid() < 10000) throw new GeneralSecurityException("Application UID required");
        if (java.security.Security.getProvider("AndroidKeyStore") != null) return;
        /* app_process skips ActivityThread's normal provider installation.
         * Invoke only Android's fixed platform installer; no custom provider,
         * hidden-API policy change, Context or exported Android component. */
        Class<?> provider;
        try { provider = Class.forName("android.security.keystore2.AndroidKeyStoreProvider"); }
        catch (ClassNotFoundException older) { provider = Class.forName("android.security.keystore.AndroidKeyStoreProvider"); }
        provider.getDeclaredMethod("install").invoke(null);
        if (java.security.Security.getProvider("AndroidKeyStore") == null) throw new GeneralSecurityException("Protected-data provider unavailable");
    }

    private static void selfTest() throws Exception {
        byte[] plain = "UT3 platform crypto canary".getBytes(StandardCharsets.UTF_8);
        byte[] entropy = {1,2,3,4,5,6,7,8};
        byte[] description = "Canary \u03a9".getBytes(StandardCharsets.UTF_16LE);
        byte[] first = transform(true, plain.clone(), entropy.clone(), description.clone())[0];
        byte[] second = transform(true, plain.clone(), entropy.clone(), description.clone())[0];
        if (Arrays.equals(first, second)) throw new GeneralSecurityException();
        byte[][] recovered = transform(false, first.clone(), entropy.clone(), new byte[0]);
        if (!Arrays.equals(plain, recovered[0]) || !Arrays.equals(description, recovered[1])) throw new GeneralSecurityException();
        Arrays.fill(recovered[0], (byte)0); Arrays.fill(recovered[1], (byte)0);
        byte[] wrong = entropy.clone(); wrong[0] ^= 1;
        boolean rejected = false;
        try { transform(false, first.clone(), wrong, new byte[0]); } catch (GeneralSecurityException expected) { rejected = true; }
        if (!rejected) throw new GeneralSecurityException();
        second[second.length - 1] ^= 1; rejected = false;
        try { transform(false, second, entropy.clone(), new byte[0]); } catch (GeneralSecurityException expected) { rejected = true; }
        if (!rejected) throw new GeneralSecurityException();
        Arrays.fill(plain, (byte)0); Arrays.fill(entropy, (byte)0); Arrays.fill(description, (byte)0); Arrays.fill(first, (byte)0);
        System.out.println("PASS AndroidKeyStore AES-GCM: real roundtrip, UTF16 description, randomized nonce, wrong entropy and tamper rejected");
    }

    /** app_process under the target app UID. ENCRYPT accepts no secret arguments:
     * stdin=UT3MIG01,u32LE entropyBytes,u32LE plainBytes,entropy,plain,EOF.
     * stdout contains only ciphertext hex. No identity root or Context needed. */
    public static void main(String[] args) {
        byte[] entropy = null, plain = null, encrypted = null;
        boolean failed = false;
        try {
            cliProvider();
            if (args.length == 1 && args[0].equals("SELFTEST")) { selfTest(); return; }
            if (args.length != 1 || !args[0].equals("ENCRYPT")) throw new IOException();
            DataInputStream input = new DataInputStream(System.in);
            byte[] magic = new byte[8]; input.readFully(magic);
            if (!Arrays.equals(magic, MIGRATION)) throw new IOException();
            int entropyBytes = littleInt(input), plainBytes = littleInt(input);
            if (entropyBytes < 0 || entropyBytes > MAX_BLOB || plainBytes < 0 || plainBytes > MAX_BLOB - 40) throw new IOException();
            entropy = new byte[entropyBytes]; plain = new byte[plainBytes]; input.readFully(entropy); input.readFully(plain);
            if (input.read() != -1) throw new IOException();
            encrypted = transform(true, plain, entropy, new byte[0])[0];
            System.out.print(hex(encrypted)); System.out.flush();
        } catch (Exception error) {
            System.err.println("Protected-data operation failed"); failed = true;
        } finally {
            if (entropy != null) Arrays.fill(entropy, (byte)0);
            if (plain != null) Arrays.fill(plain, (byte)0);
            if (encrypted != null) Arrays.fill(encrypted, (byte)0);
        }
        if (failed) System.exit(1);
    }
}
